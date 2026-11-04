#include "Historial.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "Escenarios.h"
#include "Utiles.h"

namespace Laboratorio {
namespace {

std::string numeroCsv(double valor, int decimales) {
    char bufer[64];
    std::snprintf(bufer, sizeof(bufer), "%.*f", decimales, valor);
    return bufer;
}

const char* const kCabeceraCsv =
    "id,fecha,carga,modelo,hilos,memoriaMb,pasadas,duracionMs,rssPicoKb,"
    "vmPicoKb,swapPicoKb,fallosMenores,fallosMayores,cpuPorcentaje,"
    "cambiosCtxVol,cambiosCtxInvol,cambiosCtxUsuario,pasadasCompletadas,"
    "memoriaReservadaMb,contadorEsperado,contadorFinal,actualizacionesPerdidas,"
    "estado,limiteMemoriaMb,limiteCpuSegundos,limiteCpuPorcentaje,"
    "limitesVerificados";

}

std::string muestraAJson(const Muestra& muestra, const std::string& tipo) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    if (!tipo.empty()) escritor.campo("tipo", tipo);
    escritor.campo("msDesdeInicio", muestra.msDesdeInicio);
    escritor.campo("rssKb", muestra.rssKb);
    escritor.campo("vmSizeKb", muestra.vmSizeKb);
    escritor.campo("vmSwapKb", muestra.vmSwapKb);
    escritor.campo("fallosMenores", muestra.fallosMenores);
    escritor.campo("fallosMayores", muestra.fallosMayores);
    escritor.campo("fallosPorSegundo", muestra.fallosPorSegundo);
    escritor.campo("cpuPorcentaje", muestra.cpuPorcentaje);
    escritor.campo("hilos", muestra.hilos);
    escritor.campo("progreso", muestra.progreso);
    escritor.campo("swapTotalKb", muestra.swapTotalKb);
    escritor.campo("swapLibreKb", muestra.swapLibreKb);
    escritor.campo("psiMemoriaSome", muestra.psiMemoriaSome);
    escritor.campo("psiMemoriaFull", muestra.psiMemoriaFull);
    escritor.cerrarObjeto();
    return escritor.texto();
}

std::optional<Muestra> muestraDesdeJson(const ValorJson& objeto) {
    if (!objeto.esObjeto()) return std::nullopt;

    Muestra muestra;
    muestra.msDesdeInicio    = objeto.entero("msDesdeInicio", 0);
    muestra.rssKb            = objeto.entero("rssKb", 0);
    muestra.vmSizeKb         = objeto.entero("vmSizeKb", 0);
    muestra.vmSwapKb         = objeto.entero("vmSwapKb", 0);
    muestra.fallosMenores    = objeto.entero("fallosMenores", 0);
    muestra.fallosMayores    = objeto.entero("fallosMayores", 0);
    muestra.fallosPorSegundo = objeto.real("fallosPorSegundo", 0.0);
    muestra.cpuPorcentaje    = objeto.real("cpuPorcentaje", 0.0);
    muestra.hilos            = static_cast<int>(objeto.entero("hilos", 0));
    muestra.progreso         = objeto.real("progreso", 0.0);
    muestra.swapTotalKb      = objeto.entero("swapTotalKb", 0);
    muestra.swapLibreKb      = objeto.entero("swapLibreKb", 0);
    muestra.psiMemoriaSome   = objeto.real("psiMemoriaSome", 0.0);
    muestra.psiMemoriaFull   = objeto.real("psiMemoriaFull", 0.0);
    return muestra;
}

std::string resultadoAJson(const ResultadoCorrida& resultado,
                           const std::string& tipo) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    if (!tipo.empty()) escritor.campo("tipo", tipo);
    escritor.campo("id", resultado.id);
    escritor.campo("fecha", resultado.fecha);

    escritor.campoCrudo("config", configuracionAJson(resultado.config));

    escritor.campo("duracionMs", resultado.duracionMs);
    escritor.campo("rssPicoKb", resultado.rssPicoKb);
    escritor.campo("vmPicoKb", resultado.vmPicoKb);
    escritor.campo("swapPicoKb", resultado.swapPicoKb);
    escritor.campo("fallosMenores", resultado.fallosMenores);
    escritor.campo("fallosMayores", resultado.fallosMayores);
    escritor.campo("cpuPorcentajePromedio", resultado.cpuPorcentajePromedio);
    escritor.campo("cambiosCtxVoluntarios", resultado.cambiosCtxVoluntarios);
    escritor.campo("cambiosCtxInvoluntarios", resultado.cambiosCtxInvoluntarios);
    escritor.campo("cambiosCtxUsuario", resultado.cambiosCtxUsuario);
    escritor.campo("pasadasCompletadas", resultado.pasadasCompletadas);
    escritor.campo("memoriaReservadaMb", resultado.memoriaReservadaMb);
    escritor.campo("contadorEsperado", resultado.contadorEsperado);
    escritor.campo("contadorFinal", resultado.contadorFinal);
    escritor.campo("actualizacionesPerdidas", resultado.actualizacionesPerdidas);
    escritor.campo("estado", textoEstado(resultado.estado));
    escritor.campo("senal", resultado.senal);
    escritor.campo("codigoSalida", resultado.codigoSalida);
    escritor.campo("mensaje", resultado.mensaje);
    escritor.campo("swapTotalKb", resultado.swapTotalKb);
    escritor.campo("swapLibreKb", resultado.swapLibreKb);
    escritor.campo("pswpin", resultado.pswpin);
    escritor.campo("pswpout", resultado.pswpout);
    escritor.campo("psiMemoriaPico", resultado.psiMemoriaPico);
    escritor.campo("limiteMemoriaSolicitado", resultado.limiteMemoriaSolicitado);
    escritor.campo("limiteMemoriaAplicadoKb", resultado.limiteMemoriaAplicadoKb);
    escritor.campo("limiteMemoriaVerificadoKb", resultado.limiteMemoriaVerificadoKb);
    escritor.campo("limiteCpuSegundosSolicitado", resultado.limiteCpuSegundosSolicitado);
    escritor.campo("limiteCpuSegundosAplicado", resultado.limiteCpuSegundosAplicado);
    escritor.campo("limiteCpuSegundosVerificado", resultado.limiteCpuSegundosVerificado);
    escritor.campo("limiteCpuPorcentajeAplicado", resultado.limiteCpuPorcentajeAplicado);
    escritor.campo("limiteCpuPorcentajeValor", resultado.limiteCpuPorcentajeValor);
    escritor.campo("limitesVerificados", resultado.limitesVerificados);

    escritor.clave("muestras");
    escritor.abrirArreglo();
    for (const Muestra& muestra : resultado.muestras) {
        escritor.elementoCrudo(muestraAJson(muestra));
    }
    escritor.cerrarArreglo();

    escritor.cerrarObjeto();
    return escritor.texto();
}

std::optional<ResultadoCorrida> resultadoDesdeJson(const ValorJson& objeto) {
    if (!objeto.esObjeto()) return std::nullopt;
    if (objeto.texto("id", "").empty()) return std::nullopt;

    ResultadoCorrida resultado;
    resultado.id   = objeto.texto("id", "");
    resultado.fecha = objeto.texto("fecha", "");

    const ValorJson* config = objeto.buscar("config");
    if (config != nullptr && config->esObjeto()) {
        resultado.config = configuracionDesdeJson(*config);
    }

    resultado.duracionMs              = objeto.entero("duracionMs", 0);
    resultado.rssPicoKb               = objeto.entero("rssPicoKb", 0);
    resultado.vmPicoKb                = objeto.entero("vmPicoKb", 0);
    resultado.swapPicoKb              = objeto.entero("swapPicoKb", 0);
    resultado.fallosMenores           = objeto.entero("fallosMenores", 0);
    resultado.fallosMayores           = objeto.entero("fallosMayores", 0);
    resultado.cpuPorcentajePromedio   = objeto.real("cpuPorcentajePromedio", 0.0);
    resultado.cambiosCtxVoluntarios   = objeto.entero("cambiosCtxVoluntarios", 0);
    resultado.cambiosCtxInvoluntarios = objeto.entero("cambiosCtxInvoluntarios", 0);
    resultado.cambiosCtxUsuario       = objeto.entero("cambiosCtxUsuario", 0);
    resultado.pasadasCompletadas      = objeto.real("pasadasCompletadas", 0.0);
    resultado.memoriaReservadaMb      = objeto.entero("memoriaReservadaMb", 0);
    resultado.contadorEsperado        = objeto.entero("contadorEsperado", 0);
    resultado.contadorFinal           = objeto.entero("contadorFinal", 0);
    resultado.actualizacionesPerdidas = objeto.entero("actualizacionesPerdidas", 0);
    resultado.estado                  = estadoDesdeTexto(objeto.texto("estado", "error"));
    resultado.senal                   = static_cast<int>(objeto.entero("senal", 0));
    resultado.codigoSalida            = static_cast<int>(objeto.entero("codigoSalida", 0));
    resultado.mensaje                 = objeto.texto("mensaje", "");
    resultado.swapTotalKb             = objeto.entero("swapTotalKb", 0);
    resultado.swapLibreKb             = objeto.entero("swapLibreKb", 0);
    resultado.pswpin                  = objeto.entero("pswpin", 0);
    resultado.pswpout                 = objeto.entero("pswpout", 0);
    resultado.psiMemoriaPico          = objeto.real("psiMemoriaPico", 0.0);
    resultado.limiteMemoriaSolicitado     = objeto.booleano("limiteMemoriaSolicitado", false);
    resultado.limiteMemoriaAplicadoKb     = objeto.entero("limiteMemoriaAplicadoKb", 0);
    resultado.limiteMemoriaVerificadoKb   = objeto.entero("limiteMemoriaVerificadoKb", 0);
    resultado.limiteCpuSegundosSolicitado = objeto.booleano("limiteCpuSegundosSolicitado", false);
    resultado.limiteCpuSegundosAplicado   = objeto.entero("limiteCpuSegundosAplicado", 0);
    resultado.limiteCpuSegundosVerificado = objeto.entero("limiteCpuSegundosVerificado", 0);
    resultado.limiteCpuPorcentajeAplicado = objeto.booleano("limiteCpuPorcentajeAplicado", false);
    resultado.limiteCpuPorcentajeValor    = static_cast<int>(objeto.entero("limiteCpuPorcentajeValor", 100));
    resultado.limitesVerificados          = objeto.booleano("limitesVerificados", false);

    const ValorJson* muestras = objeto.buscar("muestras");
    if (muestras != nullptr && muestras->esArreglo()) {
        resultado.muestras.reserve(muestras->elementos.size());
        for (const ValorJson& elemento : muestras->elementos) {
            std::optional<Muestra> muestra = muestraDesdeJson(elemento);
            if (muestra.has_value()) resultado.muestras.push_back(*muestra);
        }
    }

    return resultado;
}

Historial::Historial(const std::string& rutaArchivo) {
    if (!rutaArchivo.empty()) {
        m_rutaArchivo = rutaArchivo;
    } else {
        m_rutaArchivo = rutaRaizProyecto() + "/resultados/historial.jsonl";
    }

    std::error_code error;
    std::filesystem::path ruta(m_rutaArchivo);
    if (ruta.has_parent_path()) {
        std::filesystem::create_directories(ruta.parent_path(), error);
    }
}

void Historial::cargarSiHaceFalta() {
    if (m_cargado) return;
    m_cache.clear();
    m_cargado = true;

    std::ifstream archivo(m_rutaArchivo);
    if (!archivo) return;

    std::string linea;
    long numeroLinea = 0;
    while (std::getline(archivo, linea)) {
        ++numeroLinea;
        if (recortar(linea).empty()) continue;

        std::optional<ValorJson> valor = analizarJson(linea);
        if (!valor.has_value()) {
            std::fprintf(stderr,
                         "historial: línea %ld inválida en %s, se omite\n",
                         numeroLinea, m_rutaArchivo.c_str());
            continue;
        }

        std::optional<ResultadoCorrida> resultado = resultadoDesdeJson(*valor);
        if (!resultado.has_value()) {
            std::fprintf(stderr,
                         "historial: línea %ld sin id en %s, se omite\n",
                         numeroLinea, m_rutaArchivo.c_str());
            continue;
        }
        m_cache.push_back(*resultado);
    }
}

bool Historial::agregar(const ResultadoCorrida& resultado) {
    std::lock_guard<std::mutex> bloqueo(m_candado);

    std::error_code error;
    std::filesystem::path ruta(m_rutaArchivo);
    if (ruta.has_parent_path()) {
        std::filesystem::create_directories(ruta.parent_path(), error);
    }

    std::ofstream archivo(m_rutaArchivo, std::ios::app);
    if (!archivo) {
        std::fprintf(stderr, "historial: no se pudo abrir %s\n", m_rutaArchivo.c_str());
        return false;
    }

    archivo << resultadoAJson(resultado) << '\n';
    archivo.flush();
    if (!archivo) {
        std::fprintf(stderr, "historial: no se pudo escribir en %s\n", m_rutaArchivo.c_str());
        return false;
    }

    if (m_cargado) m_cache.push_back(resultado);
    return true;
}

std::vector<ResultadoCorrida> Historial::cargarTodas() {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    cargarSiHaceFalta();
    return m_cache;
}

std::size_t Historial::cantidad() {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    cargarSiHaceFalta();
    return m_cache.size();
}

std::optional<ResultadoCorrida> Historial::buscar(const std::string& id) {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    cargarSiHaceFalta();
    for (const ResultadoCorrida& resultado : m_cache) {
        if (resultado.id == id) return resultado;
    }
    return std::nullopt;
}

std::string Historial::exportarCsv(const std::vector<std::string>& ids) {
    std::vector<ResultadoCorrida> seleccion;
    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        cargarSiHaceFalta();

        if (ids.empty()) {
            seleccion = m_cache;
        } else {

            for (const std::string& id : ids) {
                for (const ResultadoCorrida& resultado : m_cache) {
                    if (resultado.id == id) {
                        seleccion.push_back(resultado);
                        break;
                    }
                }
            }
        }
    }

    std::string csv;
    csv.reserve(256 + seleccion.size() * 256);
    csv += kCabeceraCsv;
    csv += '\n';

    for (const ResultadoCorrida& resultado : seleccion) {
        const ConfiguracionCorrida& config = resultado.config;
        csv += escaparCsv(resultado.id);
        csv += ',';
        csv += escaparCsv(resultado.fecha);
        csv += ',';
        csv += textoCarga(config.carga);
        csv += ',';
        csv += textoModelo(config.modeloHilos);
        csv += ',';
        csv += std::to_string(config.hilos);
        csv += ',';
        csv += std::to_string(config.memoriaMb);
        csv += ',';
        csv += std::to_string(config.pasadas);
        csv += ',';
        csv += std::to_string(resultado.duracionMs);
        csv += ',';
        csv += std::to_string(resultado.rssPicoKb);
        csv += ',';
        csv += std::to_string(resultado.vmPicoKb);
        csv += ',';
        csv += std::to_string(resultado.swapPicoKb);
        csv += ',';
        csv += std::to_string(resultado.fallosMenores);
        csv += ',';
        csv += std::to_string(resultado.fallosMayores);
        csv += ',';
        csv += numeroCsv(resultado.cpuPorcentajePromedio, 1);
        csv += ',';
        csv += std::to_string(resultado.cambiosCtxVoluntarios);
        csv += ',';
        csv += std::to_string(resultado.cambiosCtxInvoluntarios);
        csv += ',';
        csv += std::to_string(resultado.cambiosCtxUsuario);
        csv += ',';
        csv += numeroCsv(resultado.pasadasCompletadas, 2);
        csv += ',';
        csv += std::to_string(resultado.memoriaReservadaMb);
        csv += ',';
        csv += std::to_string(resultado.contadorEsperado);
        csv += ',';
        csv += std::to_string(resultado.contadorFinal);
        csv += ',';
        csv += std::to_string(resultado.actualizacionesPerdidas);
        csv += ',';
        csv += textoEstado(resultado.estado);
        csv += ',';
        csv += std::to_string(config.limiteMemoriaMb);
        csv += ',';
        csv += std::to_string(config.limiteCpuSegundos);
        csv += ',';
        csv += std::to_string(config.limiteCpuPorcentaje);
        csv += ',';
        csv += resultado.limitesVerificados ? "true" : "false";
        csv += '\n';
    }

    return csv;
}

}
