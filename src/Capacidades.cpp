#include "Capacidades.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "LectorProc.h"

namespace Laboratorio {

namespace {

bool rlimitAsAplicable() {
    struct rlimit actual {};
    if (getrlimit(RLIMIT_AS, &actual) != 0) return false;

    pid_t hijo = fork();
    if (hijo < 0) return false;
    if (hijo == 0) {
        struct rlimit prueba {64u * 1024u * 1024u, 64u * 1024u * 1024u};
        _exit(setrlimit(RLIMIT_AS, &prueba) == 0 ? 0 : 1);
    }

    int estado = 0;
    while (waitpid(hijo, &estado, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(estado) && WEXITSTATUS(estado) == 0;
}

bool rlimitCpuAplicable() {
    struct rlimit actual {};
    return getrlimit(RLIMIT_CPU, &actual) == 0;
}

}

bool cgroupEscribible() {

    if (access("/sys/fs/cgroup/cgroup.controllers", F_OK) != 0) return false;

    std::string ruta = "/sys/fs/cgroup/laboratorio-prueba-" + std::to_string(getpid());
    if (mkdir(ruta.c_str(), 0755) != 0) return false;
    rmdir(ruta.c_str());
    return true;
}

bool psiDisponible() {
    return access("/proc/pressure/memory", R_OK) == 0;
}

std::vector<CapacidadLimite> detectarCapacidades() {
    std::vector<CapacidadLimite> capacidades;

    const bool asOk  = rlimitAsAplicable();
    const bool cpuOk = rlimitCpuAplicable();

    capacidades.push_back({
        "Límite de memoria",
        "RLIMIT_AS",
        asOk ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        asOk ? "Aplicable sin privilegios; limita el espacio de direcciones completo"
             : "El kernel rechazó la prueba de setrlimit(RLIMIT_AS)"
    });

    capacidades.push_back({
        "Presupuesto de CPU",
        "RLIMIT_CPU",
        cpuOk ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        cpuOk ? "Cuenta la CPU sumada de todos los hilos del proceso"
              : "El kernel no expone RLIMIT_CPU"
    });

    capacidades.push_back({
        "Cuota de CPU (aproximada)",
        "SIGSTOP/SIGCONT",
        NivelCapacidad::Aproximado,
        "Granularidad de ~20 ms; no es una cuota del kernel y pausa el proceso completo"
    });

    const bool cgroupOk = cgroupEscribible();
    const std::string motivoCgroup = cgroupOk
        ? "Se puede crear un subgrupo de prueba"
        : "Requiere privilegios: /sys/fs/cgroup no es escribible";

    capacidades.push_back({
        "Cuota de CPU",
        "cgroup v2 cpu.max",
        cgroupOk ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        motivoCgroup
    });

    capacidades.push_back({
        "Límite de memoria",
        "cgroup v2 memory.max",
        cgroupOk ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        motivoCgroup
    });

    const auto meminfo = leerMeminfo();
    const bool haySwap = meminfo.has_value() && meminfo->swapTotalKb > 0;
    capacidades.push_back({
        "Swap",
        "/proc/meminfo",
        haySwap ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        haySwap ? "El sistema tiene swap configurado; el uso real depende de la presión"
                : "El sistema no tiene swap configurado"
    });

    const bool psiOk = psiDisponible();
    capacidades.push_back({
        "Presión de memoria (PSI)",
        "/proc/pressure/memory",
        psiOk ? NivelCapacidad::Disponible : NivelCapacidad::NoDisponible,
        psiOk ? "Se informa some/full avg10 durante la corrida"
              : "El kernel no expone /proc/pressure/memory"
    });

    return capacidades;
}

}
