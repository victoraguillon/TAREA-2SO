#include "LectorProc.h"

#include "Utiles.h"

#include <dirent.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace Laboratorio {

namespace {

bool leerProc(const std::string& ruta, std::string& texto) {
    return leerArchivo(ruta, texto);
}

std::optional<long> valorKb(const std::string& linea) {
    std::istringstream flujo(linea);
    std::string clave;
    long valor = 0;
    if (!(flujo >> clave >> valor)) return std::nullopt;
    return valor;
}

std::optional<long> valorTrasDosPuntos(const std::string& linea) {
    std::size_t posicion = linea.find(':');
    if (posicion == std::string::npos) return std::nullopt;
    const char* inicio = linea.c_str() + posicion + 1;
    char* fin = nullptr;
    errno = 0;
    long valor = std::strtol(inicio, &fin, 10);
    if (fin == inicio || errno != 0) return std::nullopt;
    return valor;
}

}

long frecuenciaReloj() {
    static const long frecuencia = sysconf(_SC_CLK_TCK);
    return frecuencia > 0 ? frecuencia : 100;
}

std::optional<EstadoProcesoCrudo> leerEstadoProceso(int pid) {
    std::string texto;
    if (!leerProc("/proc/" + std::to_string(pid) + "/status", texto)) return std::nullopt;

    EstadoProcesoCrudo estado;
    bool encontrado = false;
    std::istringstream flujo(texto);
    std::string linea;
    while (std::getline(flujo, linea)) {
        if (linea.rfind("VmSize:", 0) == 0) {
            if (auto valor = valorKb(linea)) { estado.vmSizeKb = *valor; encontrado = true; }
        } else if (linea.rfind("VmRSS:", 0) == 0) {
            if (auto valor = valorKb(linea)) { estado.vmRssKb = *valor; encontrado = true; }
        } else if (linea.rfind("VmHWM:", 0) == 0) {
            if (auto valor = valorKb(linea)) { estado.vmHwmKb = *valor; encontrado = true; }
        } else if (linea.rfind("VmPeak:", 0) == 0) {
            if (auto valor = valorKb(linea)) { estado.vmPeakKb = *valor; encontrado = true; }
        } else if (linea.rfind("VmSwap:", 0) == 0) {
            if (auto valor = valorKb(linea)) { estado.vmSwapKb = *valor; encontrado = true; }
        } else if (linea.rfind("Threads:", 0) == 0) {
            if (auto valor = valorTrasDosPuntos(linea)) {
                estado.hilos = static_cast<int>(*valor);
                encontrado = true;
            }
        } else if (linea.rfind("voluntary_ctxt_switches:", 0) == 0) {
            if (auto valor = valorTrasDosPuntos(linea)) estado.cambiosCtxVoluntarios = *valor;
        } else if (linea.rfind("nonvoluntary_ctxt_switches:", 0) == 0) {
            if (auto valor = valorTrasDosPuntos(linea)) estado.cambiosCtxInvoluntarios = *valor;
        }
    }
    if (!encontrado) return std::nullopt;
    return estado;
}

std::optional<StatProceso> leerStatProceso(int pid) {
    std::string texto;
    if (!leerProc("/proc/" + std::to_string(pid) + "/stat", texto)) return std::nullopt;

    std::size_t cierre = texto.rfind(')');
    if (cierre == std::string::npos || cierre + 2 >= texto.size()) return std::nullopt;

    std::istringstream resto(texto.substr(cierre + 2));
    std::vector<std::string> campos;
    std::string campo;
    while (resto >> campo) campos.push_back(campo);
    if (campos.size() < 20) return std::nullopt;

    StatProceso stat;
    stat.estado = campos[0].empty() ? '?' : campos[0][0];
    try {
        stat.minflt = std::stol(campos[7]);
        stat.majflt = std::stol(campos[9]);
        stat.utime  = std::stol(campos[11]);
        stat.stime  = std::stol(campos[12]);
        stat.hilos  = std::stoi(campos[17]);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return stat;
}

std::optional<SmapsRollup> leerSmapsRollup(int pid) {
    std::string texto;
    if (!leerProc("/proc/" + std::to_string(pid) + "/smaps_rollup", texto)) return std::nullopt;

    SmapsRollup rollup;
    bool encontrado = false;
    std::istringstream flujo(texto);
    std::string linea;
    while (std::getline(flujo, linea)) {
        if (linea.rfind("Rss:", 0) == 0) {
            if (auto valor = valorKb(linea)) { rollup.rssKb = *valor; encontrado = true; }
        } else if (linea.rfind("Swap:", 0) == 0) {
            if (auto valor = valorKb(linea)) { rollup.swapKb = *valor; encontrado = true; }
        }
    }
    if (!encontrado) return std::nullopt;
    return rollup;
}

std::optional<LimitesProceso> leerLimitesProceso(int pid) {
    std::string texto;
    if (!leerProc("/proc/" + std::to_string(pid) + "/limits", texto)) return std::nullopt;

    LimitesProceso limites;
    bool encontrado = false;
    std::istringstream flujo(texto);
    std::string linea;
    while (std::getline(flujo, linea)) {
        if (linea.rfind("Max address space", 0) == 0) {
            if (linea.find("unlimited") != std::string::npos) {
                limites.memoriaBytes = -1;
            } else {
                long blando = 0;
                long duro = 0;
                if (std::sscanf(linea.c_str(), "Max address space %ld %ld", &blando, &duro) == 2) {
                    limites.memoriaBytes = blando;
                }
            }
            encontrado = true;
        } else if (linea.rfind("Max cpu time", 0) == 0) {
            if (linea.find("unlimited") != std::string::npos) {
                limites.cpuSegundos = -1;
            } else {
                long blando = 0;
                long duro = 0;
                if (std::sscanf(linea.c_str(), "Max cpu time %ld %ld", &blando, &duro) == 2) {
                    limites.cpuSegundos = blando;
                }
            }
            encontrado = true;
        }
    }
    if (!encontrado) return std::nullopt;
    return limites;
}

std::optional<Meminfo> leerMeminfo() {
    std::string texto;
    if (!leerProc("/proc/meminfo", texto)) return std::nullopt;

    Meminfo info;
    std::istringstream flujo(texto);
    std::string linea;
    while (std::getline(flujo, linea)) {
        if (linea.rfind("MemTotal:", 0) == 0) {
            if (auto valor = valorKb(linea)) info.memTotalKb = *valor;
        } else if (linea.rfind("MemAvailable:", 0) == 0) {
            if (auto valor = valorKb(linea)) info.memDisponibleKb = *valor;
        } else if (linea.rfind("SwapTotal:", 0) == 0) {
            if (auto valor = valorKb(linea)) info.swapTotalKb = *valor;
        } else if (linea.rfind("SwapFree:", 0) == 0) {
            if (auto valor = valorKb(linea)) info.swapLibreKb = *valor;
        }
    }
    return info;
}

std::optional<Vmstat> leerVmstat() {
    std::string texto;
    if (!leerProc("/proc/vmstat", texto)) return std::nullopt;

    Vmstat stat;
    std::istringstream flujo(texto);
    std::string clave;
    long valor = 0;
    while (flujo >> clave >> valor) {
        if (clave == "pswpin")       stat.pswpin = valor;
        else if (clave == "pswpout") stat.pswpout = valor;
    }
    return stat;
}

std::optional<PresionMemoria> leerPresionMemoria() {
    std::string texto;
    if (!leerProc("/proc/pressure/memory", texto)) return std::nullopt;

    PresionMemoria presion;
    bool encontrado = false;
    std::istringstream flujo(texto);
    std::string linea;
    while (std::getline(flujo, linea)) {
        double valor = 0.0;
        if (linea.rfind("some", 0) == 0 &&
            std::sscanf(linea.c_str(), "some avg10=%lf", &valor) == 1) {
            presion.someAvg10 = valor;
            encontrado = true;
        } else if (linea.rfind("full", 0) == 0 &&
                   std::sscanf(linea.c_str(), "full avg10=%lf", &valor) == 1) {
            presion.fullAvg10 = valor;
            encontrado = true;
        }
    }
    if (!encontrado) return std::nullopt;
    return presion;
}

bool existeProceso(int pid) {
    if (pid <= 0) return false;
    return access(("/proc/" + std::to_string(pid)).c_str(), F_OK) == 0;
}

std::vector<HiloCrudo> leerHilosProceso(int pid) {
    std::vector<HiloCrudo> hilos;
    std::string ruta = "/proc/" + std::to_string(pid) + "/task";
    DIR* directorio = opendir(ruta.c_str());
    if (directorio == nullptr) return hilos;

    while (dirent* entrada = readdir(directorio)) {
        if (entrada->d_name[0] == '.') continue;
        char* fin = nullptr;
        long tid = std::strtol(entrada->d_name, &fin, 10);
        if (fin == entrada->d_name || *fin != '\0') continue;

        std::string texto;
        if (!leerProc(ruta + "/" + entrada->d_name + "/stat", texto)) continue;
        std::size_t cierre = texto.rfind(')');
        if (cierre == std::string::npos || cierre + 2 >= texto.size()) continue;

        std::istringstream resto(texto.substr(cierre + 2));
        std::vector<std::string> campos;
        std::string campo;
        while (resto >> campo) campos.push_back(campo);
        if (campos.size() < 13) continue;

        HiloCrudo hilo;
        hilo.tid = static_cast<int>(tid);
        try {
            hilo.utime = std::stol(campos[11]);
            hilo.stime = std::stol(campos[12]);
        } catch (const std::exception&) {
            continue;
        }
        hilos.push_back(hilo);
    }
    closedir(directorio);
    return hilos;
}

}
