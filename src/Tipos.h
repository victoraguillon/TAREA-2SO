#ifndef LABORATORIO_TIPOS_H
#define LABORATORIO_TIPOS_H

#include <string>
#include <vector>

namespace Laboratorio {

enum class ModeloHilos { Nativos, Tareas };

enum class ModoCarga { Memoria, Sincronizacion };

enum class ModoFin { Pasadas, Duracion };

enum class ModoSincronizacion { Ninguna, Mutex, Atomico };

enum class EstadoCorrida {
    Inactiva,
    Ejecutando,
    Completada,
    Detenida,
    LimiteMemoria,
    LimiteCpu,
    Error
};

enum class NivelCapacidad { Disponible, Aproximado, NoDisponible };

// Tope absoluto de memoria del programa y colchón que se deja libre al sistema.
constexpr int  MEMORIA_MAXIMA_MB = 4096;
constexpr long MARGEN_MEMORIA_KB = 1024L * 1024L;  // 1 GiB

struct ConfiguracionCorrida {
    ModoCarga           carga               = ModoCarga::Memoria;
    ModeloHilos         modeloHilos         = ModeloHilos::Nativos;
    int                 hilos               = 1;
    int                 memoriaMb           = 128;
    int                 pasadas             = 20;
    ModoFin             modoFin             = ModoFin::Pasadas;
    int                 duracionSeg         = 5;
    ModoSincronizacion  sincronizacion      = ModoSincronizacion::Ninguna;
    long                iteraciones         = 1000000;
    int                 limiteMemoriaMb     = 0;
    int                 limiteCpuSegundos   = 0;
    int                 limiteCpuPorcentaje = 100;
};

struct Muestra {
    long   msDesdeInicio      = 0;
    long   rssKb              = 0;
    long   vmSizeKb           = 0;
    long   vmSwapKb           = 0;
    long   fallosMenores      = 0;
    long   fallosMayores      = 0;
    double fallosPorSegundo   = 0.0;
    double cpuPorcentaje      = 0.0;
    int    hilos              = 0;
    double progreso           = 0.0;
    long   swapTotalKb        = 0;
    long   swapLibreKb        = 0;
    double psiMemoriaSome     = 0.0;
    double psiMemoriaFull     = 0.0;
};

struct ResultadoCorrida {
    std::string          id;
    std::string          fecha;
    ConfiguracionCorrida config;

    long   duracionMs             = 0;
    long   rssPicoKb              = 0;
    long   vmPicoKb               = 0;
    long   swapPicoKb             = 0;
    long   fallosMenores          = 0;
    long   fallosMayores          = 0;
    double cpuPorcentajePromedio  = 0.0;
    long   cambiosCtxVoluntarios  = 0;
    long   cambiosCtxInvoluntarios = 0;
    long   cambiosCtxUsuario      = 0;

    double pasadasCompletadas     = 0.0;
    long   memoriaReservadaMb     = 0;
    long   contadorEsperado       = 0;
    long   contadorFinal          = 0;
    long   actualizacionesPerdidas = 0;

    EstadoCorrida estado = EstadoCorrida::Error;
    int    senal         = 0;
    int    codigoSalida  = 0;
    std::string mensaje;

    long   swapTotalKb    = 0;
    long   swapLibreKb    = 0;
    long   pswpin         = 0;
    long   pswpout        = 0;
    double psiMemoriaPico = 0.0;

    bool   limiteMemoriaSolicitado     = false;
    long   limiteMemoriaAplicadoKb     = 0;
    long   limiteMemoriaVerificadoKb   = 0;
    bool   limiteCpuSegundosSolicitado = false;
    long   limiteCpuSegundosAplicado   = 0;
    long   limiteCpuSegundosVerificado = 0;
    bool   limiteCpuPorcentajeAplicado = false;
    int    limiteCpuPorcentajeValor    = 100;
    bool   limitesVerificados          = false;

    std::vector<Muestra> muestras;
};

struct CapacidadLimite {
    std::string    nombre;
    std::string    mecanismo;
    NivelCapacidad nivel = NivelCapacidad::NoDisponible;
    std::string    motivo;
};

inline std::string textoModelo(ModeloHilos valor) {
    return valor == ModeloHilos::Tareas ? "tareas" : "nativos";
}

inline std::string textoCarga(ModoCarga valor) {
    return valor == ModoCarga::Sincronizacion ? "sincronizacion" : "memoria";
}

inline std::string textoModoFin(ModoFin valor) {
    return valor == ModoFin::Duracion ? "duracion" : "pasadas";
}

inline std::string textoSincronizacion(ModoSincronizacion valor) {
    switch (valor) {
        case ModoSincronizacion::Mutex:    return "mutex";
        case ModoSincronizacion::Atomico:  return "atomico";
        default:                           return "ninguna";
    }
}

inline std::string textoEstado(EstadoCorrida valor) {
    switch (valor) {
        case EstadoCorrida::Ejecutando:    return "ejecutando";
        case EstadoCorrida::Completada:    return "completada";
        case EstadoCorrida::Detenida:      return "detenida";
        case EstadoCorrida::LimiteMemoria: return "limite_memoria";
        case EstadoCorrida::LimiteCpu:     return "limite_cpu";
        case EstadoCorrida::Error:         return "error";
        default:                           return "inactiva";
    }
}

inline std::string textoNivel(NivelCapacidad valor) {
    switch (valor) {
        case NivelCapacidad::Disponible:  return "disponible";
        case NivelCapacidad::Aproximado:  return "aproximado";
        default:                          return "no_disponible";
    }
}

inline ModeloHilos modeloDesdeTexto(const std::string& texto) {
    return texto == "tareas" ? ModeloHilos::Tareas : ModeloHilos::Nativos;
}

inline ModoCarga cargaDesdeTexto(const std::string& texto) {
    return texto == "sincronizacion" ? ModoCarga::Sincronizacion : ModoCarga::Memoria;
}

inline ModoFin modoFinDesdeTexto(const std::string& texto) {
    return texto == "duracion" ? ModoFin::Duracion : ModoFin::Pasadas;
}

inline ModoSincronizacion sincronizacionDesdeTexto(const std::string& texto) {
    if (texto == "mutex")   return ModoSincronizacion::Mutex;
    if (texto == "atomico") return ModoSincronizacion::Atomico;
    return ModoSincronizacion::Ninguna;
}

inline EstadoCorrida estadoDesdeTexto(const std::string& texto) {
    if (texto == "ejecutando")     return EstadoCorrida::Ejecutando;
    if (texto == "completada")     return EstadoCorrida::Completada;
    if (texto == "detenida")       return EstadoCorrida::Detenida;
    if (texto == "limite_memoria") return EstadoCorrida::LimiteMemoria;
    if (texto == "limite_cpu")     return EstadoCorrida::LimiteCpu;
    if (texto == "error")          return EstadoCorrida::Error;
    return EstadoCorrida::Inactiva;
}

}

#endif
