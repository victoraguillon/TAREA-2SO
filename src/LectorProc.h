#ifndef LABORATORIO_LECTORPROC_H
#define LABORATORIO_LECTORPROC_H

#include <optional>
#include <string>
#include <vector>

namespace Laboratorio {

struct EstadoProcesoCrudo {
    long vmSizeKb              = 0;
    long vmRssKb               = 0;
    long vmHwmKb               = 0;
    long vmPeakKb              = 0;
    long vmSwapKb              = 0;
    int  hilos                 = 0;
    long cambiosCtxVoluntarios = 0;
    long cambiosCtxInvoluntarios = 0;
};

struct StatProceso {
    char estado = '?';
    long minflt = 0;
    long majflt = 0;
    long utime  = 0;
    long stime  = 0;
    int  hilos  = 0;
};

struct SmapsRollup {
    long rssKb  = 0;
    long swapKb = 0;
};

struct LimitesProceso {
    long memoriaBytes = -1;
    long cpuSegundos  = -1;
};

struct Meminfo {
    long memTotalKb      = 0;
    long memDisponibleKb = 0;
    long swapTotalKb     = 0;
    long swapLibreKb     = 0;
};

struct Vmstat {
    long pswpin  = 0;
    long pswpout = 0;
};

struct PresionMemoria {
    double someAvg10 = 0.0;
    double fullAvg10 = 0.0;
};

struct HiloCrudo {
    int  tid   = 0;
    long utime = 0;
    long stime = 0;
};

std::optional<EstadoProcesoCrudo> leerEstadoProceso(int pid);
std::optional<StatProceso>        leerStatProceso(int pid);
std::optional<SmapsRollup>        leerSmapsRollup(int pid);
std::optional<LimitesProceso>     leerLimitesProceso(int pid);
std::optional<Meminfo>            leerMeminfo();
std::optional<Vmstat>             leerVmstat();
std::optional<PresionMemoria>     leerPresionMemoria();
bool                              existeProceso(int pid);
std::vector<HiloCrudo>            leerHilosProceso(int pid);

long frecuenciaReloj();

}

#endif
