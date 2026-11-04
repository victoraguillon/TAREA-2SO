#include "Limites.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>

#include <sys/resource.h>
#include <unistd.h>

#include "LectorProc.h"

namespace Laboratorio {

namespace {

constexpr long PERIODO_MS = 40;

}

bool aplicarLimitesHijo(const ConfiguracionCorrida& config, int& errnoSalida) {
    errnoSalida = 0;

    if (config.limiteMemoriaMb > 0) {
        rlim_t bytes = static_cast<rlim_t>(config.limiteMemoriaMb) * 1024UL * 1024UL;
        struct rlimit limite;
        limite.rlim_cur = bytes;
        limite.rlim_max = bytes;
        if (setrlimit(RLIMIT_AS, &limite) != 0) {
            errnoSalida = errno;
            return false;
        }
    }

    if (config.limiteCpuSegundos > 0) {

        struct rlimit limite;
        limite.rlim_cur = static_cast<rlim_t>(config.limiteCpuSegundos);
        limite.rlim_max = static_cast<rlim_t>(config.limiteCpuSegundos + 1);
        if (setrlimit(RLIMIT_CPU, &limite) != 0) {
            errnoSalida = errno;
            return false;
        }
    }

    struct rlimit sinNucleo;
    sinNucleo.rlim_cur = 0;
    sinNucleo.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &sinNucleo);

    return true;
}

LimitesVerificados verificarLimites(int pid, const ConfiguracionCorrida& config) {
    LimitesVerificados resultado;
    std::optional<LimitesProceso> limites = leerLimitesProceso(pid);
    if (!limites.has_value()) return resultado;

    resultado.memoriaKb = limites->memoriaBytes < 0
                              ? -1
                              : limites->memoriaBytes / 1024;
    resultado.cpuSegundos = limites->cpuSegundos;

    bool memoriaOk = true;
    bool cpuOk     = true;

    if (config.limiteMemoriaMb > 0) {
        long esperadoKb = static_cast<long>(config.limiteMemoriaMb) * 1024L;
        memoriaOk = resultado.memoriaKb == esperadoKb;
    }
    if (config.limiteCpuSegundos > 0) {
        cpuOk = resultado.cpuSegundos == config.limiteCpuSegundos;
    }

    resultado.ok = memoriaOk && cpuOk;
    return resultado;
}

LimitadorCpu::~LimitadorCpu() {
    detener();
}

bool LimitadorCpu::iniciar(int pgid, int porcentaje) {
    detener();
    if (porcentaje >= 100) {
        m_pgid       = pgid;
        m_porcentaje = 100;
        return true;
    }
    if (porcentaje < 1) porcentaje = 1;

    m_pgid       = pgid;
    m_porcentaje = porcentaje;
    m_pausados.store(0);
    m_activo.store(true);
    m_hilo = std::thread(&LimitadorCpu::ciclo, this);
    return true;
}

void LimitadorCpu::detener() {
    if (!m_activo.exchange(false)) {
        if (m_hilo.joinable()) m_hilo.join();
        return;
    }
    if (m_hilo.joinable()) m_hilo.join();

    if (m_pgid > 0) kill(-m_pgid, SIGCONT);
}

void LimitadorCpu::ciclo() {
    const long activoMs = PERIODO_MS * m_porcentaje / 100;
    const long pausaMs  = PERIODO_MS - activoMs;

    auto esperar = [this](long totalMs) {
        for (long transcurrido = 0; transcurrido < totalMs && m_activo.load();
             transcurrido += 5) {
            long tramo = totalMs - transcurrido;
            if (tramo > 5) tramo = 5;
            std::this_thread::sleep_for(std::chrono::milliseconds(tramo));
        }
    };

    while (m_activo.load()) {
        kill(-m_pgid, SIGCONT);
        esperar(activoMs);
        if (!m_activo.load()) break;

        if (pausaMs > 0) {
            kill(-m_pgid, SIGSTOP);
            esperar(pausaMs);
            m_pausados.fetch_add(pausaMs);
            kill(-m_pgid, SIGCONT);
        }
    }
}

}
