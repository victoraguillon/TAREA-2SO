#include "Api.h"
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <optional>
#include <system_error>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "Capacidades.h"
#include "Escenarios.h"
#include "Json.h"
#include "LectorProc.h"
#include "Utiles.h"

namespace Laboratorio {

namespace {

constexpr const char* VERSION_LABORATORIO = "1.0.0";

bool enviarCrudo(int descriptor, const std::string& datos) {
    std::size_t enviado = 0;
    while (enviado < datos.size()) {
        const ssize_t escritos =
            send(descriptor, datos.data() + enviado, datos.size() - enviado,
                 MSG_NOSIGNAL);
        if (escritos < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (escritos == 0) return false;
        enviado += static_cast<std::size_t>(escritos);
    }
    return true;
}

bool enviarEvento(int descriptor, const std::string& nombre,
                  const std::string& datos) {
    std::string bloque;
    if (!nombre.empty()) bloque += "event: " + nombre + "\n";
    bloque += "data: " + datos + "\n\n";
    return enviarCrudo(descriptor, bloque);
}

RespuestaHttp respuestaJson(const std::string& cuerpo, int codigo = 200) {
    RespuestaHttp respuesta;
    respuesta.codigo = codigo;
    respuesta.cuerpo = cuerpo;
    return respuesta;
}

RespuestaHttp respuestaError(int codigo, const std::string& mensaje) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("error", mensaje);
    escritor.cerrarObjeto();
    return respuestaJson(escritor.texto(), codigo);
}

RespuestaHttp respuestaMetodoNoPermitido(const std::string& permitidos) {
    RespuestaHttp respuesta = respuestaError(405, "Método no permitido.");
    respuesta.cabeceras["Allow"] = permitidos;
    return respuesta;
}

bool textoPermitido(const ValorJson& objeto, const std::string& clave,
                    std::initializer_list<const char*> permitidos) {
    const ValorJson* valor = objeto.buscar(clave);
    if (valor == nullptr) return true;
    if (valor->tipo != ValorJson::Tipo::Texto) return false;
    for (const char* permitido : permitidos) {
        if (valor->valorTexto == permitido) return true;
    }
    return false;
}

std::vector<std::string> identificadoresDeConsulta(const std::string& consulta) {
    std::vector<std::string> identificadores;
    for (const std::string& parte : dividir(consulta, '&')) {
        if (parte.rfind("ids=", 0) != 0) continue;

        for (const std::string& id : dividir(parte.substr(4), ',')) {
            const std::string limpio = recortar(id);
            if (!limpio.empty()) identificadores.push_back(limpio);
        }
    }
    return identificadores;
}

}

Api::Api(Ejecutor& ejecutor, Historial& historial)
    : m_ejecutor(ejecutor), m_historial(historial) {}

Api::~Api() {
    detener();
}

void Api::detener() {
    m_parando.store(true);

    m_ejecutor.detener();

    std::thread hilo;
    {
        std::lock_guard<std::mutex> bloqueo(m_candadoCorrida);
        if (m_hiloCorrida.joinable()) hilo = std::move(m_hiloCorrida);
    }
    if (hilo.joinable()) hilo.join();

    m_ejecutor.eventos().cerrar();
}

RespuestaHttp Api::manejar(const PeticionHttp& peticion) {
    const std::string& ruta = peticion.ruta;
    const bool esGet  = (peticion.metodo == "GET");
    const bool esPost = (peticion.metodo == "POST");

    if (ruta == "/api/sistema") {
        return esGet ? manejarSistema() : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/capacidades") {
        return esGet ? manejarCapacidades() : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/escenarios") {
        return esGet ? manejarEscenarios() : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/corrida/iniciar") {
        return esPost ? manejarIniciar(peticion) : respuestaMetodoNoPermitido("POST");
    }
    if (ruta == "/api/corrida/detener") {
        return esPost ? manejarDetenerCorrida() : respuestaMetodoNoPermitido("POST");
    }
    if (ruta == "/api/corrida/estado") {
        return esGet ? manejarEstadoCorrida() : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/eventos") {
        return esGet ? manejarEventos(peticion) : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/resultados") {
        return esGet ? manejarResultados() : respuestaMetodoNoPermitido("GET");
    }
    if (ruta == "/api/resultados/exportar.csv") {
        return esGet ? manejarExportarCsv(peticion) : respuestaMetodoNoPermitido("GET");
    }
    if (ruta.rfind("/api/resultados/", 0) == 0) {
        if (!esGet) return respuestaMetodoNoPermitido("GET");
        return manejarResultado(ruta.substr(std::string("/api/resultados/").size()));
    }

    return respuestaError(404, "Ruta no encontrada.");
}

RespuestaHttp Api::manejarSistema() {
    const std::optional<Meminfo> memoria = leerMeminfo();

    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("nucleos", static_cast<long>(sysconf(_SC_NPROCESSORS_ONLN)));
    escritor.campo("memTotalKb", memoria.has_value() ? memoria->memTotalKb : 0);
    escritor.campo("memDisponibleKb",
                   memoria.has_value() ? memoria->memDisponibleKb : 0);
    escritor.campo("swapTotalKb", memoria.has_value() ? memoria->swapTotalKb : 0);
    escritor.campo("swapLibreKb", memoria.has_value() ? memoria->swapLibreKb : 0);
    escritor.campo("psiDisponible", psiDisponible());
    escritor.campo("cgroupEscribible", cgroupEscribible());
    escritor.campo("version", VERSION_LABORATORIO);
    escritor.cerrarObjeto();

    return respuestaJson(escritor.texto());
}

RespuestaHttp Api::manejarCapacidades() {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.clave("capacidades");
    escritor.abrirArreglo();

    for (const CapacidadLimite& capacidad : detectarCapacidades()) {
        escritor.abrirObjeto();
        escritor.campo("nombre", capacidad.nombre);
        escritor.campo("mecanismo", capacidad.mecanismo);
        escritor.campo("nivel", textoNivel(capacidad.nivel));
        escritor.campo("motivo", capacidad.motivo);
        escritor.cerrarObjeto();
    }

    escritor.cerrarArreglo();
    escritor.cerrarObjeto();

    return respuestaJson(escritor.texto());
}

RespuestaHttp Api::manejarEscenarios() {
    return respuestaJson(escenariosAJson());
}

RespuestaHttp Api::manejarIniciar(const PeticionHttp& peticion) {
    std::optional<ValorJson> valor = analizarJson(peticion.cuerpo);
    if (!valor.has_value() || !valor->esObjeto()) {
        return respuestaError(400, "El cuerpo debe ser un objeto JSON válido.");
    }

    const ConfiguracionCorrida config = configuracionDesdeJson(*valor);
    const std::string error = validarConfiguracion(config, *valor);
    if (!error.empty()) return respuestaError(400, error);

    std::string id;
    std::string errorReserva;
    if (!m_ejecutor.reservar(config, id, errorReserva)) {
        EscritorJson escritor;
        escritor.abrirObjeto();
        escritor.campo("error", errorReserva);
        escritor.campo("id", id);
        escritor.cerrarObjeto();
        return respuestaJson(escritor.texto(), 409);
    }

    {
        std::lock_guard<std::mutex> bloqueo(m_candadoCorrida);

        if (m_hiloCorrida.joinable()) m_hiloCorrida.join();

        try {
            m_hiloCorrida = std::thread([this] { m_ejecutor.ejecutar(); });
        } catch (const std::system_error&) {
            m_ejecutor.cancelarReserva(
                "No se pudo lanzar el hilo que ejecuta la corrida.");
            return respuestaError(
                500, "No se pudo lanzar el hilo que ejecuta la corrida.");
        }
    }

    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("id", id);
    escritor.campo("estado", "ejecutando");
    escritor.cerrarObjeto();
    return respuestaJson(escritor.texto(), 202);
}

RespuestaHttp Api::manejarDetenerCorrida() {
    if (!m_ejecutor.detener()) {
        return respuestaError(409, "No hay una corrida en curso.");
    }
    return respuestaJson("{\"estado\":\"deteniendo\"}", 202);
}

RespuestaHttp Api::manejarEstadoCorrida() {
    return respuestaJson(estadoAJson(m_ejecutor.estado()));
}

RespuestaHttp Api::manejarEventos(const PeticionHttp&  ) {
    std::shared_ptr<BuzonEventos::Suscripcion> suscripcion =
        m_ejecutor.eventos().suscribir();

    RespuestaHttp respuesta;
    respuesta.codigo        = 200;
    respuesta.tipoContenido = "text/event-stream; charset=utf-8";
    respuesta.cabeceras["Cache-Control"]     = "no-cache";
    respuesta.cabeceras["X-Accel-Buffering"] = "no";

    respuesta.flujo = [this, suscripcion](int descriptor) {

        if (!enviarEvento(descriptor, "estado",
                          estadoAJson(m_ejecutor.estado()))) {
            return;
        }

        long ultimoPing = relojMonotonicoMs();
        while (!m_parando.load()) {
            Evento evento;
            bool   cerrado = false;

            if (suscripcion->esperar(evento, 1000, cerrado)) {
                if (!enviarEvento(descriptor, evento.nombre, evento.datos)) return;
            } else if (cerrado) {
                return;
            }

            const long ahora = relojMonotonicoMs();
            if (ahora - ultimoPing >= 15000) {

                if (!enviarCrudo(descriptor, ": ping\n\n")) return;
                ultimoPing = ahora;
            }
        }
    };

    return respuesta;
}

RespuestaHttp Api::manejarResultados() {
    std::vector<ResultadoCorrida> resultados = m_historial.cargarTodas();

    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.clave("resultados");
    escritor.abrirArreglo();

    for (auto actual = resultados.rbegin(); actual != resultados.rend(); ++actual) {
        const ResultadoCorrida& resultado = *actual;
        escritor.abrirObjeto();
        escritor.campo("id", resultado.id);
        escritor.campo("fecha", resultado.fecha);
        escritor.campo("estado", textoEstado(resultado.estado));
        escritor.campo("carga", textoCarga(resultado.config.carga));
        escritor.campo("modeloHilos", textoModelo(resultado.config.modeloHilos));
        escritor.campo("hilos", resultado.config.hilos);
        escritor.campo("duracionMs", resultado.duracionMs);
        escritor.campo("rssPicoKb", resultado.rssPicoKb);
        escritor.campo("vmPicoKb", resultado.vmPicoKb);
        escritor.campo("swapPicoKb", resultado.swapPicoKb);
        escritor.campo("fallosMenores", resultado.fallosMenores);
        escritor.campo("fallosMayores", resultado.fallosMayores);
        escritor.campo("cpuPorcentajePromedio", resultado.cpuPorcentajePromedio);
        escritor.campo("pasadasCompletadas", resultado.pasadasCompletadas);
        escritor.campo("actualizacionesPerdidas", resultado.actualizacionesPerdidas);

        escritor.campo("limiteMemoriaMb", resultado.config.limiteMemoriaMb);
        escritor.campo("limiteCpuSegundos", resultado.config.limiteCpuSegundos);
        escritor.campo("limiteCpuPorcentaje", resultado.config.limiteCpuPorcentaje);
        escritor.campo("limiteCpuPorcentajeAplicado",
                       resultado.limiteCpuPorcentajeAplicado);
        escritor.campo("limitesVerificados", resultado.limitesVerificados);
        escritor.cerrarObjeto();
    }

    escritor.cerrarArreglo();
    escritor.cerrarObjeto();
    return respuestaJson(escritor.texto());
}

RespuestaHttp Api::manejarResultado(const std::string& id) {
    std::optional<ResultadoCorrida> resultado = m_historial.buscar(id);
    if (!resultado.has_value()) {
        return respuestaError(404, "No existe una corrida con ese identificador.");
    }
    return respuestaJson(resultadoAJson(*resultado));
}

RespuestaHttp Api::manejarExportarCsv(const PeticionHttp& peticion) {
    const std::vector<std::string> ids =
        identificadoresDeConsulta(peticion.consulta);

    RespuestaHttp respuesta;
    respuesta.codigo        = 200;
    respuesta.tipoContenido = "text/csv; charset=utf-8";
    respuesta.cuerpo        = m_historial.exportarCsv(ids);
    respuesta.cabeceras["Content-Disposition"] =
        "attachment; filename=\"resultados.csv\"";
    return respuesta;
}

std::string Api::validarConfiguracion(const ConfiguracionCorrida& config,
                                      const ValorJson& objeto) const {

    if (!textoPermitido(objeto, "carga", {"memoria", "sincronizacion"})) {
        return "carga inválida: debe ser 'memoria' o 'sincronizacion'.";
    }
    if (!textoPermitido(objeto, "modeloHilos", {"nativos", "tareas"})) {
        return "modelo de hilos inválido: debe ser 'nativos' o 'tareas'.";
    }
    if (!textoPermitido(objeto, "modoFin", {"pasadas", "duracion"})) {
        return "modo de fin inválido: debe ser 'pasadas' o 'duracion'.";
    }
    if (!textoPermitido(objeto, "sincronizacion",
                        {"ninguna", "mutex", "atomico"})) {
        return "sincronización inválida: debe ser 'ninguna', 'mutex' o 'atomico'.";
    }

    const long nucleos = sysconf(_SC_NPROCESSORS_ONLN);
    const int  topeHilos = static_cast<int>(std::min<long>(8 * nucleos, 32));

    if (config.hilos < 1 || config.hilos > topeHilos) {
        return "cantidad de hilos fuera de rango (1.." +
               std::to_string(topeHilos) + ").";
    }
    if (config.memoriaMb < 1 || config.memoriaMb > MEMORIA_MAXIMA_MB) {
        return "memoria fuera de rango (1.." +
               std::to_string(MEMORIA_MAXIMA_MB) + " MB).";
    }
    if (config.pasadas < 1 || config.pasadas > 1000) {
        return "pasadas fuera de rango (1..1000).";
    }
    if (config.duracionSeg < 1 || config.duracionSeg > 180) {
        return "duración máxima 180 s.";
    }
    if (config.iteraciones < 1 || config.iteraciones > 100000000L) {
        return "iteraciones fuera de rango (1..100000000).";
    }
    if (config.limiteMemoriaMb != 0 &&
        (config.limiteMemoriaMb < 8 ||
         config.limiteMemoriaMb > MEMORIA_MAXIMA_MB)) {
        return "límite de memoria fuera de rango (0 u 8.." +
               std::to_string(MEMORIA_MAXIMA_MB) + " MB).";
    }
    if (config.limiteCpuSegundos < 0 || config.limiteCpuSegundos > 600) {
        return "límite de CPU fuera de rango (0..600 s).";
    }
    if (config.limiteCpuPorcentaje < 1 || config.limiteCpuPorcentaje > 100) {
        return "porcentaje de CPU fuera de rango (1..100).";
    }

    if (config.carga == ModoCarga::Memoria) {
        if (std::optional<Meminfo> memoria = leerMeminfo()) {
            if (memoria->memDisponibleKb > 0) {
                const long topeKb = memoria->memDisponibleKb - MARGEN_MEMORIA_KB;
                const long pedidoKb =
                    static_cast<long>(config.memoriaMb) * 1024L +
                    static_cast<long>(config.hilos) * 512L;
                if (pedidoKb > topeKb) {
                    const long maximoSeguroMb = (topeKb > 0) ? topeKb / 1024 : 0;
                    return "memoria solicitada excede el máximo seguro de " +
                           std::to_string(maximoSeguroMb) +
                           " MB (se reserva 1 GB para el sistema).";
                }
            }
        }
    }

    return std::string();
}

}
