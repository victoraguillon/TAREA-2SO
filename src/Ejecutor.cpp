#include "Ejecutor.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <system_error>
#include <thread>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "Escenarios.h"
#include "LectorProc.h"
#include "Limites.h"
#include "Utiles.h"

namespace Laboratorio {

namespace {

constexpr std::size_t MAX_EVENTOS_POR_CANAL = 64;

constexpr long INTERVALO_ESPERA_MS = 20;

constexpr long ESPERA_SIGKILL_MS = 2000;

constexpr long INTERVALO_MUESTRA_MS = 250;
constexpr long PRIMERA_MUESTRA_MS   = 100;

constexpr long PRIMERA_VERIFICACION_MS = 150;
constexpr long LIMITE_VERIFICACION_MS  = 1100;
constexpr long REINTENTO_VERIFICACION_MS = 200;

constexpr long MARGEN_WATCHDOG_MS = 15000;
constexpr long WATCHDOG_PASADAS_MS = 300000;

constexpr int MAX_DESCRIPTORES_HIJO = 1024;

constexpr std::size_t MAX_LINEA_NDJSON = 1024 * 1024;

std::string mensajePorEstado(EstadoCorrida estado) {
    switch (estado) {
        case EstadoCorrida::Completada:
            return "Corrida completada.";
        case EstadoCorrida::Detenida:
            return "Corrida detenida por el usuario.";
        case EstadoCorrida::LimiteMemoria:
            return "Se alcanzó el límite de memoria virtual (RLIMIT_AS).";
        case EstadoCorrida::LimiteCpu:
            return "Se agotó el presupuesto de CPU (RLIMIT_CPU).";
        case EstadoCorrida::Ejecutando:
            return "La corrida sigue en ejecución.";
        case EstadoCorrida::Inactiva:
            return "No hay ninguna corrida.";
        default:
            return "La corrida terminó con un error.";
    }
}

}

struct CanalEventos {
    std::mutex              candado;
    std::condition_variable aviso;
    std::deque<Evento>      cola;
    bool                    cerrado = false;
};

BuzonEventos::Suscripcion::~Suscripcion() = default;

bool BuzonEventos::Suscripcion::esperar(Evento& evento, long esperaMs,
                                        bool& cerrado) {
    cerrado = false;
    if (!m_canal) {
        cerrado = true;
        return false;
    }

    std::unique_lock<std::mutex> bloqueo(m_canal->candado);
    m_canal->aviso.wait_for(
        bloqueo, std::chrono::milliseconds(esperaMs),
        [this] { return !m_canal->cola.empty() || m_canal->cerrado; });

    if (!m_canal->cola.empty()) {
        evento = std::move(m_canal->cola.front());
        m_canal->cola.pop_front();
        return true;
    }

    cerrado = m_canal->cerrado;
    return false;
}

BuzonEventos::~BuzonEventos() = default;

std::shared_ptr<BuzonEventos::Suscripcion> BuzonEventos::suscribir() {
    auto canal       = std::make_shared<CanalEventos>();
    auto suscripcion = std::make_shared<Suscripcion>();
    suscripcion->m_canal = canal;

    std::lock_guard<std::mutex> bloqueo(m_candado);
    if (m_cerrado) {
        canal->cerrado = true;
    } else {
        m_canales.push_back(canal);
    }
    return suscripcion;
}

void BuzonEventos::publicar(const Evento& evento) {

    std::vector<std::shared_ptr<CanalEventos>> canales;
    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        if (m_cerrado) return;

        auto actual = m_canales.begin();
        while (actual != m_canales.end()) {
            if (std::shared_ptr<CanalEventos> canal = actual->lock()) {
                canales.push_back(std::move(canal));
                ++actual;
            } else {
                actual = m_canales.erase(actual);
            }
        }
    }

    for (const std::shared_ptr<CanalEventos>& canal : canales) {
        std::lock_guard<std::mutex> bloqueo(canal->candado);
        if (canal->cerrado) continue;

        if (canal->cola.size() >= MAX_EVENTOS_POR_CANAL) {
            canal->cola.pop_front();
        }
        canal->cola.push_back(evento);
        canal->aviso.notify_one();
    }
}

void BuzonEventos::cerrar() {
    std::vector<std::shared_ptr<CanalEventos>> canales;
    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        if (m_cerrado) return;
        m_cerrado = true;

        for (const std::weak_ptr<CanalEventos>& debil : m_canales) {
            if (std::shared_ptr<CanalEventos> canal = debil.lock()) {
                canales.push_back(std::move(canal));
            }
        }
        m_canales.clear();
    }

    for (const std::shared_ptr<CanalEventos>& canal : canales) {
        std::lock_guard<std::mutex> bloqueo(canal->candado);
        canal->cerrado = true;
        canal->aviso.notify_all();
    }
}

std::string estadoAJson(const EstadoEjecucion& estado) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("activo", estado.activo);
    escritor.campo("id", estado.id);
    escritor.campo("estado", textoEstado(estado.estado));
    escritor.campoCrudo("config", configuracionAJson(estado.config));

    if (estado.hayMuestra) {
        escritor.campoCrudo("muestra", muestraAJson(estado.ultimaMuestra));
    } else {
        escritor.campoNulo("muestra");
    }

    escritor.campo("progreso", estado.progreso);
    escritor.campo("msDesdeInicio", estado.msDesdeInicio);
    escritor.campo("pid", estado.pid);
    escritor.campo("mensaje", estado.mensaje);
    escritor.cerrarObjeto();
    return escritor.texto();
}

Ejecutor::Ejecutor(Historial& historial) : m_historial(historial) {}

bool Ejecutor::reservar(const ConfiguracionCorrida& config, std::string& id,
                        std::string& error) {
    std::lock_guard<std::mutex> bloqueo(m_candado);

    if (m_activo) {
        id    = m_id;
        error = "ya hay una corrida en curso";
        return false;
    }

    m_activo              = true;
    m_detenerSolicitado   = false;
    m_fecha               = fechaIso8601Local();
    m_id                  = idCorrida(m_fecha, static_cast<int>(m_historial.cantidad()) + 1);
    m_config              = config;
    m_estado              = EstadoCorrida::Ejecutando;
    m_hayMuestra          = false;
    m_ultimaMuestra       = Muestra{};
    m_progreso            = 0.0;
    m_inicioMs            = relojMonotonicoMs();
    m_finMs               = 0;
    m_pid                 = 0;
    m_mensaje.clear();

    m_muestreando.store(false);
    m_muestras.clear();

    m_hayLimitesTrabajador    = false;
    m_limitesOkTrabajador     = false;
    m_hayResultadoTrabajador  = false;
    m_resultadoTrabajador     = ResultadoCorrida{};
    m_hayJiffies              = false;
    m_utimeTrabajador         = 0;
    m_stimeTrabajador         = 0;

    m_hayLimitesVerificados = false;
    m_limitesVerificados    = LimitesVerificados{};

    id = m_id;
    return true;
}

void Ejecutor::cancelarReserva(const std::string& motivo) {
    std::lock_guard<std::mutex> bloqueo(m_candado);

    if (!m_activo || m_pid != 0) return;

    m_activo  = false;
    m_estado  = EstadoCorrida::Error;
    m_mensaje = motivo;
    m_finMs   = relojMonotonicoMs();
}

bool Ejecutor::detener() {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    if (!m_activo) return false;

    m_detenerSolicitado = true;
    return true;
}

bool Ejecutor::activo() const {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    return m_activo;
}

EstadoEjecucion Ejecutor::estado() const {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    return estadoBloqueado();
}

Muestra Ejecutor::ultimaMuestra() const {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    return m_ultimaMuestra;
}

ConfiguracionCorrida Ejecutor::configuracionActual() const {
    std::lock_guard<std::mutex> bloqueo(m_candado);
    return m_config;
}

EstadoEjecucion Ejecutor::estadoBloqueado() const {
    EstadoEjecucion estado;
    estado.activo       = m_activo;
    estado.id           = m_id;
    estado.estado       = m_estado;
    estado.config       = m_config;
    estado.hayMuestra   = m_hayMuestra;
    estado.ultimaMuestra = m_ultimaMuestra;
    estado.progreso     = m_progreso;
    estado.pid          = m_pid;
    estado.mensaje      = m_mensaje;

    const long instante = (m_finMs > 0) ? m_finMs : relojMonotonicoMs();
    estado.msDesdeInicio = (m_inicioMs > 0) ? std::max(0L, instante - m_inicioMs) : 0;
    return estado;
}

void Ejecutor::publicarEstado() {
    Evento evento;
    evento.nombre = "estado";
    evento.datos  = estadoAJson(estado());
    m_eventos.publicar(evento);
}

void Ejecutor::publicarFin(const ResultadoCorrida& resultado) {

    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("id", resultado.id);
    escritor.campo("estado", textoEstado(resultado.estado));
    escritor.cerrarObjeto();

    Evento evento;
    evento.nombre = "fin";
    evento.datos  = escritor.texto();
    m_eventos.publicar(evento);
}

void Ejecutor::leerSalida(int descriptor) {

    DescriptorArchivo archivo(descriptor);

    std::string acumulado;
    char        bufer[4096];

    while (true) {
        const ssize_t leidos = ::read(archivo.obtener(), bufer, sizeof(bufer));
        if (leidos < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (leidos == 0) break;

        acumulado.append(bufer, static_cast<std::size_t>(leidos));

        std::size_t posicion = 0;
        while ((posicion = acumulado.find('\n')) != std::string::npos) {
            procesarLinea(acumulado.substr(0, posicion));
            acumulado.erase(0, posicion + 1);
        }

        if (acumulado.size() > MAX_LINEA_NDJSON) acumulado.clear();
    }

    if (!acumulado.empty()) procesarLinea(acumulado);
}

void Ejecutor::procesarLinea(const std::string& linea) {
    const std::string limpia = recortar(linea);
    if (limpia.empty()) return;

    std::optional<ValorJson> valor = analizarJson(limpia);
    if (!valor.has_value() || !valor->esObjeto()) return;

    const std::string tipo = valor->texto("tipo", "");

    if (tipo == "inicio") {
        const bool limitesOk = valor->booleano("limitesOk", false);
        int pid = 0;
        ConfiguracionCorrida config;
        {
            std::lock_guard<std::mutex> bloqueo(m_candado);
            m_hayLimitesTrabajador = true;
            m_limitesOkTrabajador  = limitesOk;
            pid                    = m_pid;
            config                 = m_config;
        }

        if (pid > 0) {
            const LimitesVerificados verificados = verificarLimites(pid, config);
            std::lock_guard<std::mutex> bloqueo(m_candado);
            m_hayLimitesVerificados = true;
            m_limitesVerificados    = verificados;
        }
        return;
    }

    if (tipo == "progreso") {
        const double progreso = valor->real("progreso", 0.0);
        std::lock_guard<std::mutex> bloqueo(m_candado);
        if (progreso > m_progreso) m_progreso = progreso;
        return;
    }

    if (tipo == "resultado") {
        std::optional<ResultadoCorrida> resultado = resultadoDesdeJson(*valor);
        if (!resultado.has_value()) return;

        const long utime = valor->entero("utime", 0);
        const long stime = valor->entero("stime", 0);

        std::lock_guard<std::mutex> bloqueo(m_candado);
        m_resultadoTrabajador    = *resultado;
        m_hayResultadoTrabajador = true;
        m_hayJiffies             = true;
        m_utimeTrabajador        = utime;
        m_stimeTrabajador        = stime;
        if (resultado->pasadasCompletadas > m_progreso) {
            m_progreso = std::min(1.0, resultado->pasadasCompletadas);
        }
        return;
    }
}

void Ejecutor::muestrear(int pid, long inicioMs,
                         const ConfiguracionCorrida& config) {
    const long hz = frecuenciaReloj();

    long proximaMuestra      = inicioMs + PRIMERA_MUESTRA_MS;
    long proximaVerificacion = inicioMs + PRIMERA_VERIFICACION_MS;
    const long topeVerificacion = inicioMs + LIMITE_VERIFICACION_MS;

    long utimeAnterior   = 0;
    long stimeAnterior   = 0;
    long fallosAnterior  = 0;
    long instanteAnterior = inicioMs;
    bool verificacionHecha = false;

    while (m_muestreando.load()) {
        const long ahora = relojMonotonicoMs();

        if (!verificacionHecha) {
            {
                std::lock_guard<std::mutex> bloqueo(m_candado);
                verificacionHecha = m_hayLimitesVerificados;
            }

            if (!verificacionHecha && ahora >= proximaVerificacion &&
                ahora <= topeVerificacion) {
                const LimitesVerificados verificados =
                    verificarLimites(pid, config);
                std::lock_guard<std::mutex> bloqueo(m_candado);
                if (!m_hayLimitesVerificados && verificados.ok) {
                    m_hayLimitesVerificados = true;
                    m_limitesVerificados    = verificados;
                }
                verificacionHecha = m_hayLimitesVerificados;
                proximaVerificacion = ahora + REINTENTO_VERIFICACION_MS;
            }
        }

        if (ahora >= proximaMuestra) {

            bool arrancado = false;
            {
                std::lock_guard<std::mutex> bloqueo(m_candado);
                arrancado = m_hayLimitesTrabajador;
            }

            std::optional<EstadoProcesoCrudo> estadoProc =
                arrancado ? leerEstadoProceso(pid) : std::nullopt;
            std::optional<StatProceso> statProc =
                arrancado ? leerStatProceso(pid) : std::nullopt;

            const bool procesoVivo =
                estadoProc.has_value() && estadoProc->vmSizeKb > 0;

            if (procesoVivo && statProc.has_value()) {
                Muestra muestra;
                muestra.msDesdeInicio = ahora - inicioMs;
                muestra.rssKb         = estadoProc->vmRssKb;
                muestra.vmSizeKb      = estadoProc->vmSizeKb;
                muestra.vmSwapKb      = estadoProc->vmSwapKb;
                muestra.fallosMenores = statProc->minflt;
                muestra.fallosMayores = statProc->majflt;
                muestra.hilos         = statProc->hilos;

                const long transcurrido = ahora - instanteAnterior;
                if (transcurrido > 0) {
                    const long jiffies =
                        (statProc->utime + statProc->stime) -
                        (utimeAnterior + stimeAnterior);
                    muestra.cpuPorcentaje =
                        static_cast<double>(jiffies) * 100000.0 /
                        static_cast<double>(hz * transcurrido);

                    const long fallos = statProc->minflt + statProc->majflt;
                    muestra.fallosPorSegundo =
                        static_cast<double>(fallos - fallosAnterior) * 1000.0 /
                        static_cast<double>(transcurrido);
                }

                utimeAnterior    = statProc->utime;
                stimeAnterior    = statProc->stime;
                fallosAnterior   = statProc->minflt + statProc->majflt;
                instanteAnterior = ahora;

                if (std::optional<Meminfo> memoria = leerMeminfo()) {
                    muestra.swapTotalKb = memoria->swapTotalKb;
                    muestra.swapLibreKb = memoria->swapLibreKb;
                }
                if (std::optional<PresionMemoria> presion = leerPresionMemoria()) {
                    muestra.psiMemoriaSome = presion->someAvg10;
                    muestra.psiMemoriaFull = presion->fullAvg10;
                }

                {
                    std::lock_guard<std::mutex> bloqueo(m_candado);
                    muestra.progreso = m_progreso;
                    m_ultimaMuestra  = muestra;
                    m_hayMuestra     = true;
                    m_muestras.push_back(muestra);
                }

                Evento evento;
                evento.nombre = "muestra";
                evento.datos  = muestraAJson(muestra);
                m_eventos.publicar(evento);
            }

            proximaMuestra = ahora + INTERVALO_MUESTRA_MS;
        }

        dormirMilisegundos(25);
    }
}

ResultadoCorrida Ejecutor::ejecutar() {
    std::string          id;
    std::string          fecha;
    ConfiguracionCorrida config;
    bool                 detencionPrevia = false;

    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        if (!m_activo) {
            ResultadoCorrida resultado;
            resultado.id     = m_id;
            resultado.fecha  = m_fecha;
            resultado.config = m_config;
            resultado.estado = EstadoCorrida::Error;
            resultado.mensaje = "La corrida se canceló antes de empezar.";
            return resultado;
        }
        id             = m_id;
        fecha          = m_fecha;
        config         = m_config;
        detencionPrevia = m_detenerSolicitado;
    }

    if (detencionPrevia) {
        ResultadoCorrida resultado;
        resultado.id      = id;
        resultado.fecha   = fecha;
        resultado.config  = config;
        resultado.estado  = EstadoCorrida::Detenida;
        resultado.mensaje = mensajePorEstado(EstadoCorrida::Detenida);

        {
            std::lock_guard<std::mutex> bloqueo(m_candado);
            m_activo  = false;
            m_estado  = EstadoCorrida::Detenida;
            m_mensaje = resultado.mensaje;
            m_finMs   = relojMonotonicoMs();
        }

        m_historial.agregar(resultado);
        publicarEstado();
        publicarFin(resultado);
        return resultado;
    }

    const std::string ejecutable = rutaEjecutable();
    if (ejecutable.empty()) {
        return finalizarConError(id, fecha, config,
                                 "No se pudo resolver la ruta del ejecutable.");
    }

    const std::string configJson = configuracionAJson(config);
    char* const argumentos[] = {
        const_cast<char*>(ejecutable.c_str()),
        const_cast<char*>("--trabajador"),
        const_cast<char*>(configJson.c_str()),
        nullptr};

    const std::optional<Vmstat> vmstatInicial = leerVmstat();

    int tubo[2] = {-1, -1};
    if (pipe(tubo) != 0) {
        return finalizarConError(id, fecha, config,
                                 "No se pudo crear el conducto de salida del trabajador.");
    }

    fcntl(tubo[0], F_SETFD, FD_CLOEXEC);

    const long inicioMs = relojMonotonicoMs();
    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        m_inicioMs = inicioMs;
        m_estado   = EstadoCorrida::Ejecutando;
        m_mensaje.clear();
    }
    publicarEstado();

    const pid_t pid = fork();
    if (pid < 0) {
        ::close(tubo[0]);
        ::close(tubo[1]);
        return finalizarConError(id, fecha, config,
                                 "No se pudo crear el proceso trabajador (fork).");
    }

    if (pid == 0) {

        setpgid(0, 0);

        prctl(PR_SET_PDEATHSIG, SIGKILL);

        int errnoLimites = 0;
        if (!aplicarLimitesHijo(config, errnoLimites)) {
            _exit(121);
        }

        if (dup2(tubo[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }

        for (int descriptor = 3; descriptor < MAX_DESCRIPTORES_HIJO; ++descriptor) {
            ::close(descriptor);
        }

        execv(ejecutable.c_str(), argumentos);
        _exit(127);
    }

    ::close(tubo[1]);
    tubo[1] = -1;

    if (setpgid(pid, pid) != 0 && errno != EACCES) {

    }

    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        m_pid = static_cast<int>(pid);
    }

    LimitadorCpu limitador;
    limitador.iniciar(static_cast<int>(pid), config.limiteCpuPorcentaje);

    m_muestreando.store(true);

    const int descriptorLectura = tubo[0];
    tubo[0] = -1;

    std::thread hiloMuestreador;
    try {
        hiloMuestreador =
            std::thread(&Ejecutor::muestrear, this, static_cast<int>(pid),
                        inicioMs, config);
    } catch (const std::system_error&) {
        m_muestreando.store(false);
        ::close(descriptorLectura);
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int estadoHijo = 0;
        waitpid(pid, &estadoHijo, 0);
        limitador.detener();
        return finalizarConError(
            id, fecha, config,
            "No se pudieron lanzar los hilos de control del supervisor.");
    }

    std::thread hiloLector;
    try {
        hiloLector = std::thread(&Ejecutor::leerSalida, this, descriptorLectura);
    } catch (const std::system_error&) {
        m_muestreando.store(false);
        ::close(descriptorLectura);
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        int estadoHijo = 0;
        waitpid(pid, &estadoHijo, 0);
        limitador.detener();
        hiloMuestreador.join();
        return finalizarConError(
            id, fecha, config,
            "No se pudieron lanzar los hilos de control del supervisor.");
    }

    bool hayEstado         = false;
    int  estadoHijo        = 0;
    bool detenidoPorUsuario = false;
    bool vencidoWatchdog   = false;
    long instanteTerminacion = 0;

    const long watchdogMs =
        (config.modoFin == ModoFin::Duracion)
            ? static_cast<long>(config.duracionSeg) * 1000 + MARGEN_WATCHDOG_MS
            : WATCHDOG_PASADAS_MS;
    const long instanteWatchdog = inicioMs + watchdogMs;

    while (true) {
        const pid_t esperado = waitpid(pid, &estadoHijo, WNOHANG);
        if (esperado == pid) {
            hayEstado = true;
            break;
        }
        if (esperado < 0 && errno != EINTR) {

            break;
        }

        const long ahora = relojMonotonicoMs();

        bool pedidoDeDetencion = false;
        {
            std::lock_guard<std::mutex> bloqueo(m_candado);
            pedidoDeDetencion = m_detenerSolicitado;
        }

        if (pedidoDeDetencion) {
            if (instanteTerminacion == 0) {
                instanteTerminacion = ahora;
                detenidoPorUsuario  = true;

                kill(-pid, SIGTERM);
                kill(pid, SIGTERM);
            } else if (ahora - instanteTerminacion >= ESPERA_SIGKILL_MS) {
                kill(-pid, SIGKILL);
                kill(pid, SIGKILL);
            }
        }

        if (!vencidoWatchdog && ahora >= instanteWatchdog) {
            vencidoWatchdog = true;
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
        }

        dormirMilisegundos(INTERVALO_ESPERA_MS);
    }

    m_muestreando.store(false);
    limitador.detener();

    hiloMuestreador.join();
    hiloLector.join();

    const long finMs = relojMonotonicoMs();

    ResultadoCorrida resultado;
    bool hayResultadoTrabajador = false;
    long utimeTrabajador = 0;
    long stimeTrabajador = 0;
    bool hayJiffies = false;
    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        hayResultadoTrabajador = m_hayResultadoTrabajador;
        if (hayResultadoTrabajador) resultado = m_resultadoTrabajador;
        utimeTrabajador = m_utimeTrabajador;
        stimeTrabajador = m_stimeTrabajador;
        hayJiffies      = m_hayJiffies;
    }

    resultado.id     = id;
    resultado.fecha  = fecha;
    resultado.config = config;
    resultado.duracionMs = std::max(0L, finMs - inicioMs);
    resultado.senal      = 0;
    resultado.codigoSalida = 0;

    if (hayEstado && WIFSIGNALED(estadoHijo)) {
        resultado.senal = WTERMSIG(estadoHijo);
    }
    if (hayEstado && WIFEXITED(estadoHijo)) {
        resultado.codigoSalida = WEXITSTATUS(estadoHijo);
    }

    long rssPico  = resultado.rssPicoKb;
    long vmPico   = resultado.vmPicoKb;
    long swapPico = resultado.swapPicoKb;
    double psiPico = resultado.psiMemoriaPico;

    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        resultado.muestras = m_muestras;
        if (!hayResultadoTrabajador) {
            resultado.pasadasCompletadas = m_progreso;
        }
    }

    double sumaCpu = 0.0;
    for (const Muestra& muestra : resultado.muestras) {
        rssPico  = std::max(rssPico, muestra.rssKb);
        vmPico   = std::max(vmPico, muestra.vmSizeKb);
        swapPico = std::max(swapPico, muestra.vmSwapKb);
        psiPico  = std::max(psiPico, muestra.psiMemoriaSome);
        sumaCpu += muestra.cpuPorcentaje;
    }
    resultado.rssPicoKb     = rssPico;
    resultado.vmPicoKb      = vmPico;
    resultado.swapPicoKb    = swapPico;
    resultado.psiMemoriaPico = psiPico;

    if (!hayResultadoTrabajador && !resultado.muestras.empty()) {
        const Muestra& ultima = resultado.muestras.back();
        resultado.fallosMenores = ultima.fallosMenores;
        resultado.fallosMayores = ultima.fallosMayores;
        resultado.swapTotalKb   = ultima.swapTotalKb;
        resultado.swapLibreKb   = ultima.swapLibreKb;
    }

    const long hz = frecuenciaReloj();
    if (hayJiffies && resultado.duracionMs > 0 && hz > 0) {
        const double segundosCpu =
            static_cast<double>(utimeTrabajador + stimeTrabajador) /
            static_cast<double>(hz);
        resultado.cpuPorcentajePromedio =
            segundosCpu * 100000.0 / static_cast<double>(resultado.duracionMs);
    } else if (!resultado.muestras.empty()) {
        resultado.cpuPorcentajePromedio =
            sumaCpu / static_cast<double>(resultado.muestras.size());
    }

    std::optional<Vmstat> vmstatFinal = leerVmstat();
    if (vmstatFinal.has_value() && vmstatInicial.has_value()) {
        resultado.pswpin  = std::max(0L, vmstatFinal->pswpin - vmstatInicial->pswpin);
        resultado.pswpout = std::max(0L, vmstatFinal->pswpout - vmstatInicial->pswpout);
    }

    if (resultado.swapTotalKb == 0 && resultado.swapLibreKb == 0) {
        if (std::optional<Meminfo> memoria = leerMeminfo()) {
            resultado.swapTotalKb = memoria->swapTotalKb;
            resultado.swapLibreKb = memoria->swapLibreKb;
        }
    }

    // El techo de memoria se aplica siempre (pedido o 4096 MB), así que
    // siempre hay algo que verificar.
    bool hayLimitePedido = true;
    bool limitesVerificados = false;
    long memoriaVerificadaKb = 0;
    long cpuVerificadoSegundos = 0;

    if (hayLimitePedido) {
        bool hayVerificacionSupervisor = false;
        LimitesVerificados verificados;
        bool hayReporteTrabajador = false;
        bool trabajadorOk = false;
        {
            std::lock_guard<std::mutex> bloqueo(m_candado);
            hayVerificacionSupervisor = m_hayLimitesVerificados;
            verificados               = m_limitesVerificados;
            hayReporteTrabajador      = m_hayLimitesTrabajador;
            trabajadorOk              = m_limitesOkTrabajador;
        }

        if (hayVerificacionSupervisor && verificados.ok) {
            limitesVerificados = true;
            memoriaVerificadaKb = verificados.memoriaKb;
            if (config.limiteCpuSegundos > 0) cpuVerificadoSegundos = verificados.cpuSegundos;
        } else if (hayReporteTrabajador && trabajadorOk) {

            limitesVerificados = true;
            memoriaVerificadaKb =
                static_cast<long>(techoMemoriaMb(config)) * 1024L;
            if (config.limiteCpuSegundos > 0) {
                cpuVerificadoSegundos = config.limiteCpuSegundos;
            }
        }
    }

    resultado.limitesVerificados = limitesVerificados;
    resultado.limiteMemoriaSolicitado = (config.limiteMemoriaMb > 0);
    resultado.limiteCpuSegundosSolicitado = (config.limiteCpuSegundos > 0);

    resultado.limiteMemoriaAplicadoKb =
        limitesVerificados ? memoriaVerificadaKb : 0;
    resultado.limiteMemoriaVerificadoKb =
        limitesVerificados ? memoriaVerificadaKb : 0;
    resultado.limiteCpuSegundosAplicado =
        (limitesVerificados && config.limiteCpuSegundos > 0) ? cpuVerificadoSegundos : 0;
    resultado.limiteCpuSegundosVerificado =
        (limitesVerificados && config.limiteCpuSegundos > 0) ? cpuVerificadoSegundos : 0;

    resultado.limiteCpuPorcentajeAplicado =
        (config.limiteCpuPorcentaje > 0 && config.limiteCpuPorcentaje < 100);
    resultado.limiteCpuPorcentajeValor = config.limiteCpuPorcentaje;

    EstadoCorrida estadoFinal = EstadoCorrida::Error;
    std::string mensajeFinal;

    if (!hayEstado) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "No se pudo obtener el estado de salida del trabajador.";
    } else if (vencidoWatchdog) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "El trabajador excedió el tiempo máximo de vigilancia y fue terminado.";
    } else if (resultado.senal == SIGXCPU) {
        estadoFinal  = EstadoCorrida::LimiteCpu;
        mensajeFinal = "Se agotó el presupuesto de CPU: el kernel envió SIGXCPU.";
    } else if (resultado.codigoSalida == 120) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "El trabajador no pudo aplicar los límites pedidos; la corrida no se considera válida.";
    } else if (resultado.codigoSalida == 121) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "setrlimit falló en el proceso trabajador.";
    } else if (resultado.codigoSalida == 127) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "No se pudo ejecutar el proceso trabajador.";
    } else if (hayResultadoTrabajador &&
               resultado.estado != EstadoCorrida::Inactiva) {
        estadoFinal = resultado.estado;
        mensajeFinal = resultado.mensaje;
    } else if (detenidoPorUsuario || resultado.senal == SIGTERM) {
        estadoFinal  = EstadoCorrida::Detenida;
        mensajeFinal = mensajePorEstado(EstadoCorrida::Detenida);
    } else if (resultado.codigoSalida == 0) {
        estadoFinal  = EstadoCorrida::Completada;
        mensajeFinal = mensajePorEstado(EstadoCorrida::Completada);
    } else if (resultado.codigoSalida == 2) {
        estadoFinal  = EstadoCorrida::LimiteMemoria;
        mensajeFinal = mensajePorEstado(EstadoCorrida::LimiteMemoria);
    } else if (resultado.codigoSalida == 3) {
        estadoFinal  = EstadoCorrida::Detenida;
        mensajeFinal = mensajePorEstado(EstadoCorrida::Detenida);
    } else if (resultado.codigoSalida == 4) {
        estadoFinal  = EstadoCorrida::LimiteCpu;
        mensajeFinal = mensajePorEstado(EstadoCorrida::LimiteCpu);
    } else if (resultado.senal != 0) {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "El trabajador terminó por la señal " +
                       std::to_string(resultado.senal) + ".";
    } else {
        estadoFinal  = EstadoCorrida::Error;
        mensajeFinal = "El trabajador terminó con código " +
                       std::to_string(resultado.codigoSalida) + ".";
    }

    if (mensajeFinal.empty()) mensajeFinal = mensajePorEstado(estadoFinal);
    resultado.estado  = estadoFinal;
    resultado.mensaje = mensajeFinal;

    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        m_estado  = estadoFinal;
        m_finMs   = finMs;
        m_mensaje = mensajeFinal;
        m_pid     = 0;
        m_activo  = false;
        if (estadoFinal == EstadoCorrida::Completada) m_progreso = 1.0;
    }

    m_historial.agregar(resultado);

    publicarEstado();
    publicarFin(resultado);

    return resultado;
}

ResultadoCorrida Ejecutor::finalizarConError(const std::string& id,
                                             const std::string& fecha,
                                             const ConfiguracionCorrida& config,
                                             const std::string& mensaje) {
    ResultadoCorrida resultado;
    resultado.id      = id;
    resultado.fecha   = fecha;
    resultado.config  = config;
    resultado.estado  = EstadoCorrida::Error;
    resultado.mensaje = mensaje;

    {
        std::lock_guard<std::mutex> bloqueo(m_candado);
        m_estado  = EstadoCorrida::Error;
        m_mensaje = mensaje;
        m_finMs   = relojMonotonicoMs();
        m_pid     = 0;
        m_activo  = false;
    }

    m_historial.agregar(resultado);
    publicarEstado();
    publicarFin(resultado);
    return resultado;
}

}
