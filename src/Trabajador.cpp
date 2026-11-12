#include "Trabajador.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <malloc.h>
#include <mutex>
#include <string>
#include <vector>

#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include "Historial.h"
#include "Json.h"
#include "LectorProc.h"
#include "Tareas.h"
#include "Utiles.h"

namespace Laboratorio {
namespace {

volatile sig_atomic_t g_detener  = 0;
volatile sig_atomic_t g_agotoCpu = 0;

std::mutex        g_candadoSalida;
std::atomic<long> g_ultimoAvisoMs{0};

std::atomic<long> g_sumidero{0};

constexpr std::size_t TAM_PAGINA             = 4096;
constexpr long        BYTES_BLOQUE           = 16L * 1024 * 1024;
constexpr long        PAGINAS_POR_BLOQUE     = BYTES_BLOQUE / static_cast<long>(TAM_PAGINA);

constexpr std::size_t TAM_PILA_HILO          = 512 * 1024;

constexpr long        ITERACIONES_POR_PAGINA = 4096;
constexpr long        INTERVALO_AVISO_MS     = 200;
constexpr long        PAGINAS_POR_CONTROL    = 64;
constexpr long        PAGINAS_POR_CESION     = 256;

constexpr long        ITERACIONES_POR_CONTROL = 4096;
constexpr long        LOTE_CONTADOR           = 4096;

constexpr long        ITERACIONES_POR_CESION_RACHA = 64;

struct Bloque {
    unsigned char* base  = nullptr;
    std::size_t    bytes = 0;
};

struct Contexto {
    const ConfiguracionCorrida* config = nullptr;

    std::vector<Bloque> bloques;
    std::size_t         bytesReservados = 0;
    long                paginasTotales  = 0;

    alignas(64) std::atomic<long> contador{0};
    std::mutex                    candadoContador;

    std::atomic<long> paginasHechas{0};
    std::atomic<long> iteracionesHechas{0};
    std::atomic<bool> abortar{false};

    long cambiosCtxUsuario = 0;
    long inicioMs          = 0;
};

void emitirLinea(const std::string& linea) {
    std::lock_guard<std::mutex> guardia(g_candadoSalida);
    std::string conSalto = linea;
    conSalto.push_back('\n');
    escribirTodo(STDOUT_FILENO, conSalto);
}

double progresoActual(const Contexto& contexto) {
    const ConfiguracionCorrida& config = *contexto.config;

    if (config.carga == ModoCarga::Sincronizacion) {
        const double total = static_cast<double>(config.hilos) *
                             static_cast<double>(config.iteraciones);
        if (total <= 0.0) return 0.0;
        const double hecho = static_cast<double>(
            contexto.iteracionesHechas.load(std::memory_order_relaxed));
        return std::min(1.0, hecho / total);
    }

    if (config.modoFin == ModoFin::Duracion) {
        const long totalMs = static_cast<long>(config.duracionSeg) * 1000;
        if (totalMs <= 0) return 0.0;
        const long transcurrido = relojMonotonicoMs() - contexto.inicioMs;
        return std::min(1.0, static_cast<double>(transcurrido) /
                                 static_cast<double>(totalMs));
    }

    const double total = static_cast<double>(contexto.paginasTotales) *
                         static_cast<double>(config.pasadas);
    if (total <= 0.0) return 0.0;
    const double hecho = static_cast<double>(
        contexto.paginasHechas.load(std::memory_order_relaxed));
    return std::min(1.0, hecho / total);
}

void avisarProgreso(const Contexto& contexto) {
    const long ahora = relojMonotonicoMs();
    long anterior = g_ultimoAvisoMs.load(std::memory_order_relaxed);
    if (ahora - anterior < INTERVALO_AVISO_MS) return;
    if (!g_ultimoAvisoMs.compare_exchange_strong(anterior, ahora,
                                                 std::memory_order_relaxed)) {
        return;
    }

    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("tipo", "progreso");
    escritor.campo("msDesdeInicio", ahora - contexto.inicioMs);
    escritor.campo("progreso", progresoActual(contexto));
    escritor.campo("paginasHechas",
                   contexto.paginasHechas.load(std::memory_order_relaxed));
    escritor.campo("iteracionesHechas",
                   contexto.iteracionesHechas.load(std::memory_order_relaxed));
    escritor.cerrarObjeto();
    emitirLinea(escritor.texto());
}

bool reservarMemoria(Contexto& contexto, long bytesObjetivo) {
    if (bytesObjetivo <= 0) return true;

    const std::size_t cantidadBloques =
        static_cast<std::size_t>((bytesObjetivo + BYTES_BLOQUE - 1) / BYTES_BLOQUE);
    contexto.bloques.reserve(cantidadBloques);

    long restante = bytesObjetivo;
    while (restante > 0) {
        const std::size_t tam =
            static_cast<std::size_t>(std::min<long>(BYTES_BLOQUE, restante));
        void* reserva = mmap(nullptr, tam, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (reserva == MAP_FAILED) return false;

        Bloque bloque;
        bloque.base  = static_cast<unsigned char*>(reserva);
        bloque.bytes = tam;
        contexto.bloques.push_back(bloque);
        contexto.bytesReservados += tam;
        restante -= static_cast<long>(tam);
    }
    return true;
}

void liberarMemoria(Contexto& contexto) {
    for (Bloque& bloque : contexto.bloques) {
        if (bloque.base != nullptr) munmap(bloque.base, bloque.bytes);
    }
    contexto.bloques.clear();
}

unsigned char* direccionPagina(const Contexto& contexto, long pagina) {
    if (pagina < 0 || pagina >= contexto.paginasTotales) return nullptr;

    const std::size_t indiceBloque =
        static_cast<std::size_t>(pagina / PAGINAS_POR_BLOQUE);
    if (indiceBloque >= contexto.bloques.size()) return nullptr;

    const std::size_t desplazamiento =
        static_cast<std::size_t>(pagina % PAGINAS_POR_BLOQUE) * TAM_PAGINA;
    const Bloque& bloque = contexto.bloques[indiceBloque];
    if (desplazamiento + TAM_PAGINA > bloque.bytes) return nullptr;

    return bloque.base + desplazamiento;
}

void repartirRebanada(long paginasTotales, int hilos, int indice,
                      long& inicio, long& cantidad) {
    if (hilos <= 0) {
        inicio   = 0;
        cantidad = 0;
        return;
    }

    const long base  = paginasTotales / hilos;
    const long resto = paginasTotales % hilos;

    inicio   = static_cast<long>(indice) * base + std::min<long>(indice, resto);
    cantidad = base + (indice < resto ? 1 : 0);
}

void cuerpoMemoria(Contexto& contexto, int indice) {
    const ConfiguracionCorrida& config = *contexto.config;

    long inicio = 0;
    long cantidad = 0;
    repartirRebanada(contexto.paginasTotales, config.hilos, indice, inicio, cantidad);

    const bool porDuracion = (config.modoFin == ModoFin::Duracion);
    const long duracionMs  = static_cast<long>(config.duracionSeg) * 1000;
    const long pasadas     = porDuracion ? std::numeric_limits<long>::max()
                                         : static_cast<long>(config.pasadas);

    const long cesionCada = cantidad > 1024 ? PAGINAS_POR_CESION
                                            : std::max<long>(1, cantidad / 4);

    unsigned long semilla = 0x2545F491UL +
                            static_cast<unsigned long>(indice) * 2654435761UL;

    long porControl  = 0;
    long porCesion   = 0;
    bool terminar    = false;

    for (long pasada = 0; pasada < pasadas && !terminar; ++pasada) {
        for (long p = 0; p < cantidad; ++p) {
            if (g_detener != 0 ||
                contexto.abortar.load(std::memory_order_relaxed)) {
                terminar = true;
                break;
            }
            if (porDuracion && relojMonotonicoMs() - contexto.inicioMs >= duracionMs) {
                terminar = true;
                break;
            }

            unsigned char* direccion = direccionPagina(contexto, inicio + p);
            if (direccion == nullptr) {
                contexto.abortar.store(true, std::memory_order_relaxed);
                terminar = true;
                break;
            }

            volatile unsigned char* cursor = direccion;
            const unsigned char valor = static_cast<unsigned char>(pasada + indice);
            for (std::size_t desplazamiento = 0; desplazamiento < TAM_PAGINA;
                 desplazamiento += 64) {
                cursor[desplazamiento] = valor;
            }

            for (long repeticion = 0; repeticion < ITERACIONES_POR_PAGINA;
                 ++repeticion) {
                semilla = semilla * 1664525UL + 1013904223UL;
            }

            contexto.paginasHechas.fetch_add(1, std::memory_order_relaxed);

            if (++porControl >= PAGINAS_POR_CONTROL) {
                porControl = 0;
                avisarProgreso(contexto);
            }
            if (++porCesion >= cesionCada) {
                porCesion = 0;
                cederTarea();
            }
        }
    }

    g_sumidero.fetch_add(static_cast<long>(semilla), std::memory_order_relaxed);
}

void cuerpoSincronizacion(Contexto& contexto, int indice) {
    const ConfiguracionCorrida& config = *contexto.config;
    const long iteraciones = static_cast<long>(config.iteraciones);
    const ModoSincronizacion modo = config.sincronizacion;

    unsigned long semilla = 0x9E3779B9UL +
                            static_cast<unsigned long>(indice) * 2654435761UL;

    long pendientes   = 0;
    long porControl   = 0;
    long porCesion    = 0;

    for (long i = 0; i < iteraciones; ++i) {
        if (g_detener != 0 || contexto.abortar.load(std::memory_order_relaxed)) break;

        switch (modo) {
            case ModoSincronizacion::Ninguna: {
                const long valor =
                    contexto.contador.load(std::memory_order_relaxed);

                if (++porCesion >= ITERACIONES_POR_CESION_RACHA) {
                    porCesion = 0;
                    cederTarea();
                }
                contexto.contador.store(valor + 1, std::memory_order_relaxed);
                break;
            }
            case ModoSincronizacion::Mutex: {
                std::lock_guard<std::mutex> guardia(contexto.candadoContador);
                const long valor =
                    contexto.contador.load(std::memory_order_relaxed);
                contexto.contador.store(valor + 1, std::memory_order_relaxed);
                break;
            }
            default: {
                contexto.contador.fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }

        if (++pendientes >= LOTE_CONTADOR) {
            contexto.iteracionesHechas.fetch_add(pendientes,
                                                 std::memory_order_relaxed);
            pendientes = 0;
            avisarProgreso(contexto);
        }

        if (modo != ModoSincronizacion::Ninguna &&
            ++porControl >= ITERACIONES_POR_CONTROL) {
            porControl = 0;
            cederTarea();
        }
    }

    if (pendientes > 0) {
        contexto.iteracionesHechas.fetch_add(pendientes, std::memory_order_relaxed);
    }
    g_sumidero.fetch_add(static_cast<long>(semilla), std::memory_order_relaxed);
}

void ejecutarCuerpo(Contexto& contexto, int indice) {
    if (contexto.config->carga == ModoCarga::Sincronizacion) {
        cuerpoSincronizacion(contexto, indice);
    } else {
        cuerpoMemoria(contexto, indice);
    }
}

struct ArgumentoHilo {
    Contexto* contexto = nullptr;
    int       indice   = 0;
};

void* entradaHiloNativo(void* argumento) {
    ArgumentoHilo* datos = static_cast<ArgumentoHilo*>(argumento);
    ejecutarCuerpo(*datos->contexto, datos->indice);
    return nullptr;
}

bool ejecutarNativos(Contexto& contexto) {
    const int hilos = contexto.config->hilos;
    if (hilos <= 0) return false;

    const int auxiliares = hilos - 1;

    std::vector<ArgumentoHilo> argumentos(static_cast<std::size_t>(auxiliares));
    std::vector<pthread_t>     identificadores(static_cast<std::size_t>(auxiliares), 0);
    std::vector<bool>          creados(static_cast<std::size_t>(auxiliares), false);

    pthread_attr_t atributos;
    if (pthread_attr_init(&atributos) != 0) return false;
    pthread_attr_setstacksize(&atributos, TAM_PILA_HILO);

    bool completo = true;
    for (int i = 0; i < auxiliares; ++i) {
        argumentos[static_cast<std::size_t>(i)].contexto = &contexto;
        argumentos[static_cast<std::size_t>(i)].indice   = i + 1;

        if (pthread_create(&identificadores[static_cast<std::size_t>(i)], &atributos,
                           entradaHiloNativo,
                           &argumentos[static_cast<std::size_t>(i)]) != 0) {
            completo = false;
            break;
        }
        creados[static_cast<std::size_t>(i)] = true;
    }

    pthread_attr_destroy(&atributos);

    if (!completo) contexto.abortar.store(true, std::memory_order_relaxed);

    ejecutarCuerpo(contexto, 0);

    for (int i = 0; i < auxiliares; ++i) {
        if (creados[static_cast<std::size_t>(i)]) {
            pthread_join(identificadores[static_cast<std::size_t>(i)], nullptr);
        }
    }
    return completo;
}

bool ejecutarTareas(Contexto& contexto) {
    PlanificadorTareas planificador;

    const bool completo = planificador.ejecutar(
        contexto.config->hilos,
        [&contexto](int indice) {
            if (contexto.config->carga == ModoCarga::Sincronizacion) {
                cuerpoSincronizacion(contexto, indice);
            } else {
                cuerpoMemoria(contexto, indice);
            }
        });

    contexto.cambiosCtxUsuario = planificador.cambiosDeContexto();
    return completo;
}

void manejadorSenal(int numero) {
    if (numero == SIGXCPU) g_agotoCpu = 1;
    g_detener = 1;
}

void instalarManejadores() {
    struct sigaction accion;
    std::memset(&accion, 0, sizeof(accion));
    accion.sa_handler = manejadorSenal;
    sigemptyset(&accion.sa_mask);
    accion.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &accion, nullptr);
    sigaction(SIGINT, &accion, nullptr);
    sigaction(SIGXCPU, &accion, nullptr);
}

bool verificarLimitesPropios(const ConfiguracionCorrida& config) {
    const std::optional<LimitesProceso> limites =
        leerLimitesProceso(static_cast<int>(getpid()));
    if (!limites.has_value()) return false;

    const long esperado =
        static_cast<long>(techoMemoriaMb(config)) * 1024L * 1024L;
    if (limites->memoriaBytes != esperado) return false;

    if (config.limiteCpuSegundos > 0) {
        if (limites->cpuSegundos != static_cast<long>(config.limiteCpuSegundos)) {
            return false;
        }
    }
    return true;
}

void emitirInicio(const ConfiguracionCorrida& config, bool limitesOk) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("tipo", "inicio");
    escritor.campo("pid", static_cast<long>(getpid()));
    escritor.campo("hilos", config.hilos);
    escritor.campo("modeloHilos", textoModelo(config.modeloHilos));
    escritor.campo("carga", textoCarga(config.carga));
    escritor.campo("limiteMemoriaBytes",
                   static_cast<long>(techoMemoriaMb(config)) * 1024L * 1024L);
    escritor.campo("limiteCpuSegundos", config.limiteCpuSegundos);
    escritor.campo("limitesOk", limitesOk);
    escritor.cerrarObjeto();
    emitirLinea(escritor.texto());
}

void completarMetricas(ResultadoCorrida& resultado) {
    const int pid = static_cast<int>(getpid());

    if (const std::optional<EstadoProcesoCrudo> estado = leerEstadoProceso(pid)) {
        resultado.rssPicoKb = std::max(resultado.rssPicoKb, estado->vmHwmKb);
        resultado.vmPicoKb  = std::max(resultado.vmPicoKb, estado->vmPeakKb);
        resultado.swapPicoKb = std::max(resultado.swapPicoKb, estado->vmSwapKb);
        resultado.cambiosCtxVoluntarios   = estado->cambiosCtxVoluntarios;
        resultado.cambiosCtxInvoluntarios = estado->cambiosCtxInvoluntarios;
    }

    if (const std::optional<StatProceso> stat = leerStatProceso(pid)) {
        resultado.fallosMenores = stat->minflt;
        resultado.fallosMayores = stat->majflt;
    }

    if (const std::optional<Meminfo> memoria = leerMeminfo()) {
        resultado.swapTotalKb = memoria->swapTotalKb;
        resultado.swapLibreKb = memoria->swapLibreKb;
    }

    if (const std::optional<Vmstat> vm = leerVmstat()) {
        resultado.pswpin  = vm->pswpin;
        resultado.pswpout = vm->pswpout;
    }

    if (const std::optional<PresionMemoria> presion = leerPresionMemoria()) {
        resultado.psiMemoriaPico = presion->someAvg10;
    }
}

void emitirResultado(const ResultadoCorrida& resultado) {
    std::string linea = resultadoAJson(resultado, "resultado");

    long utime = 0;
    long stime = 0;
    if (const std::optional<StatProceso> stat =
            leerStatProceso(static_cast<int>(getpid()))) {
        utime = stat->utime;
        stime = stat->stime;
    }

    if (!linea.empty() && linea.back() == '}') {
        const std::string extra = ",\"utime\":" + std::to_string(utime) +
                                  ",\"stime\":" + std::to_string(stime);
        linea.insert(linea.size() - 1, extra);
    }
    emitirLinea(linea);
}

}

int ejecutarTrabajador(const ConfiguracionCorrida& config) {

    (void)mallopt(M_ARENA_MAX, 1);

    signal(SIGPIPE, SIG_IGN);
    instalarManejadores();

    Contexto contexto;
    contexto.config   = &config;
    contexto.inicioMs = relojMonotonicoMs();

    const bool limitesOk = verificarLimitesPropios(config);
    emitirInicio(config, limitesOk);
    if (!limitesOk) return 120;

    ResultadoCorrida resultado;
    resultado.id    = "trabajador";
    resultado.fecha = fechaIso8601Local();
    resultado.config = config;

    resultado.limiteMemoriaSolicitado =
        (config.limiteMemoriaMb > 0);
    resultado.limiteMemoriaAplicadoKb =
        static_cast<long>(techoMemoriaMb(config)) * 1024L;
    resultado.limiteCpuSegundosSolicitado = (config.limiteCpuSegundos > 0);
    resultado.limiteCpuSegundosAplicado   =
        (config.limiteCpuSegundos > 0) ? config.limiteCpuSegundos : 0;
    resultado.limiteCpuPorcentajeAplicado =
        (config.limiteCpuPorcentaje > 0 && config.limiteCpuPorcentaje < 100);
    resultado.limiteCpuPorcentajeValor = config.limiteCpuPorcentaje;

    bool falloReserva = false;
    bool falloHilos   = false;

    if (config.carga == ModoCarga::Memoria) {
        const long bytesObjetivo =
            static_cast<long>(config.memoriaMb) * 1024L * 1024L;
        if (!reservarMemoria(contexto, bytesObjetivo)) {
            falloReserva = true;
        }

        contexto.paginasTotales =
            static_cast<long>(contexto.bytesReservados / TAM_PAGINA);
    }

    if (!falloReserva) {
        const bool completo = (config.modeloHilos == ModeloHilos::Tareas)
                                  ? ejecutarTareas(contexto)
                                  : ejecutarNativos(contexto);
        if (!completo) falloHilos = true;
    }

    resultado.memoriaReservadaMb =
        static_cast<long>(contexto.bytesReservados / (1024L * 1024L));

    if (config.carga == ModoCarga::Memoria && contexto.paginasTotales > 0) {
        const long hechas = contexto.paginasHechas.load(std::memory_order_relaxed);
        resultado.pasadasCompletadas =
            static_cast<double>(hechas) /
            static_cast<double>(contexto.paginasTotales);
    }

    if (config.carga == ModoCarga::Sincronizacion) {
        resultado.contadorEsperado =
            static_cast<long>(config.hilos) * static_cast<long>(config.iteraciones);
        resultado.contadorFinal =
            contexto.contador.load(std::memory_order_relaxed);
        resultado.actualizacionesPerdidas =
            resultado.contadorEsperado - resultado.contadorFinal;
        if (resultado.actualizacionesPerdidas < 0) {
            resultado.actualizacionesPerdidas = 0;
        }
    }

    completarMetricas(resultado);
    resultado.cambiosCtxUsuario = contexto.cambiosCtxUsuario;

    liberarMemoria(contexto);

    resultado.duracionMs = relojMonotonicoMs() - contexto.inicioMs;

    if (falloReserva || falloHilos) {
        if (falloReserva) {
            resultado.estado = EstadoCorrida::LimiteMemoria;
            resultado.mensaje =
                "No se pudo reservar toda la memoria pedida: el techo de " +
                std::to_string(techoMemoriaMb(config)) +
                " MB (RLIMIT_AS) lo impidió. Reservado: " +
                std::to_string(resultado.memoriaReservadaMb) + " MB de " +
                std::to_string(config.memoriaMb) + " MB.";
        } else if (config.limiteMemoriaMb > 0) {
            resultado.estado = EstadoCorrida::LimiteMemoria;
            resultado.mensaje =
                "No se pudieron crear todos los hilos dentro del límite de "
                "espacio de direcciones (RLIMIT_AS).";
        } else {
            resultado.estado = EstadoCorrida::Error;
            resultado.mensaje =
                "No se pudieron crear todos los hilos de la carga.";
        }
    } else if (g_agotoCpu != 0) {
        resultado.estado  = EstadoCorrida::LimiteCpu;
        resultado.mensaje = "El proceso agotó el presupuesto de CPU (RLIMIT_CPU).";
    } else if (g_detener != 0) {
        resultado.estado  = EstadoCorrida::Detenida;
        resultado.mensaje = "Corrida detenida a pedido del usuario.";
    } else {
        resultado.estado = EstadoCorrida::Completada;
    }

    int codigo = 0;
    switch (resultado.estado) {
        case EstadoCorrida::LimiteMemoria: codigo = 2; break;
        case EstadoCorrida::Detenida:      codigo = 3; break;
        case EstadoCorrida::LimiteCpu:     codigo = 4; break;
        case EstadoCorrida::Error:         codigo = 1; break;
        default:                           codigo = 0; break;
    }
    resultado.senal        = 0;
    resultado.codigoSalida = codigo;

    emitirResultado(resultado);
    return codigo;
}

}
