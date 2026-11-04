#ifndef LABORATORIO_LIMITES_H
#define LABORATORIO_LIMITES_H

#include <atomic>
#include <thread>

#include "Tipos.h"

namespace Laboratorio {

struct LimitesVerificados {
    long memoriaKb    = -1;
    long cpuSegundos  = -1;
    bool ok           = false;
};

bool aplicarLimitesHijo(const ConfiguracionCorrida& config, int& errnoSalida);

LimitesVerificados verificarLimites(int pid, const ConfiguracionCorrida& config);

class LimitadorCpu {
public:
    LimitadorCpu() = default;
    ~LimitadorCpu();

    LimitadorCpu(const LimitadorCpu&) = delete;
    LimitadorCpu& operator=(const LimitadorCpu&) = delete;

    bool iniciar(int pgid, int porcentaje);

    void detener();

    bool activo() const { return m_activo.load(); }

    long milisegundosPausados() const { return m_pausados.load(); }

private:
    void ciclo();

    int               m_pgid       = -1;
    int               m_porcentaje = 100;
    std::atomic<bool> m_activo{false};
    std::atomic<long> m_pausados{0};
    std::thread       m_hilo;
};

}

#endif
