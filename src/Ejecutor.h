#ifndef LABORATORIO_EJECUTOR_H
#define LABORATORIO_EJECUTOR_H

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Historial.h"
#include "Limites.h"
#include "Tipos.h"

namespace Laboratorio {

struct CanalEventos;

struct Evento {
    std::string nombre;
    std::string datos;
};

class BuzonEventos {
public:
    class Suscripcion {
    public:
        Suscripcion() = default;
        ~Suscripcion();

        Suscripcion(const Suscripcion&) = delete;
        Suscripcion& operator=(const Suscripcion&) = delete;

        bool esperar(Evento& evento, long esperaMs, bool& cerrado);

    private:
        friend class BuzonEventos;
        std::shared_ptr<CanalEventos> m_canal;
    };

    BuzonEventos() = default;
    ~BuzonEventos();

    BuzonEventos(const BuzonEventos&) = delete;
    BuzonEventos& operator=(const BuzonEventos&) = delete;

    std::shared_ptr<Suscripcion> suscribir();
    void publicar(const Evento& evento);
    void cerrar();

private:
    std::mutex                               m_candado;
    std::vector<std::weak_ptr<CanalEventos>> m_canales;
    bool                                     m_cerrado = false;
};

struct EstadoEjecucion {
    bool                 activo       = false;
    std::string          id;
    EstadoCorrida        estado       = EstadoCorrida::Inactiva;
    ConfiguracionCorrida config;
    bool                 hayMuestra   = false;
    Muestra              ultimaMuestra;
    double               progreso     = 0.0;
    long                 msDesdeInicio = 0;
    int                  pid          = 0;
    std::string          mensaje;
};

std::string estadoAJson(const EstadoEjecucion& estado);

class Ejecutor {
public:
    explicit Ejecutor(Historial& historial);

    Ejecutor(const Ejecutor&) = delete;
    Ejecutor& operator=(const Ejecutor&) = delete;

    bool reservar(const ConfiguracionCorrida& config, std::string& id,
                  std::string& error);

    ResultadoCorrida ejecutar();

    bool detener();

    void cancelarReserva(const std::string& motivo);

    bool activo() const;
    EstadoEjecucion estado() const;
    Muestra ultimaMuestra() const;
    ConfiguracionCorrida configuracionActual() const;

    BuzonEventos& eventos() { return m_eventos; }

private:

    void leerSalida(int descriptor);
    void procesarLinea(const std::string& linea);
    void muestrear(int pid, long inicioMs, const ConfiguracionCorrida& config);

    void publicarEstado();
    void publicarFin(const ResultadoCorrida& resultado);
    EstadoEjecucion estadoBloqueado() const;

    ResultadoCorrida finalizarConError(const std::string& id,
                                       const std::string& fecha,
                                       const ConfiguracionCorrida& config,
                                       const std::string& mensaje);

    Historial&   m_historial;
    BuzonEventos m_eventos;

    mutable std::mutex m_candado;

    bool                 m_activo            = false;
    bool                 m_detenerSolicitado = false;
    std::string          m_id;
    std::string          m_fecha;
    ConfiguracionCorrida m_config;
    EstadoCorrida        m_estado   = EstadoCorrida::Inactiva;
    bool                 m_hayMuestra = false;
    Muestra              m_ultimaMuestra;
    double               m_progreso = 0.0;
    long                 m_inicioMs = 0;
    long                 m_finMs    = 0;
    int                  m_pid      = 0;
    std::string          m_mensaje;

    std::atomic<bool> m_muestreando{false};
    std::vector<Muestra> m_muestras;

    bool                     m_hayLimitesTrabajador = false;
    bool                     m_limitesOkTrabajador  = false;
    bool                     m_hayResultadoTrabajador = false;
    ResultadoCorrida         m_resultadoTrabajador;
    bool                     m_hayJiffies = false;
    long                     m_utimeTrabajador = 0;
    long                     m_stimeTrabajador = 0;

    bool              m_hayLimitesVerificados = false;
    LimitesVerificados m_limitesVerificados;
};

}

#endif
