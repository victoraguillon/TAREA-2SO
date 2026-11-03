#ifndef LABORATORIO_API_H
#define LABORATORIO_API_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "Ejecutor.h"
#include "Historial.h"
#include "ServidorHttp.h"

namespace Laboratorio {

class Api {
public:
    Api(Ejecutor& ejecutor, Historial& historial);
    ~Api();

    Api(const Api&) = delete;
    Api& operator=(const Api&) = delete;

    RespuestaHttp manejar(const PeticionHttp& peticion);

    void detener();

private:
    RespuestaHttp manejarSistema();
    RespuestaHttp manejarCapacidades();
    RespuestaHttp manejarEscenarios();
    RespuestaHttp manejarIniciar(const PeticionHttp& peticion);
    RespuestaHttp manejarDetenerCorrida();
    RespuestaHttp manejarEstadoCorrida();
    RespuestaHttp manejarEventos(const PeticionHttp& peticion);
    RespuestaHttp manejarResultados();
    RespuestaHttp manejarResultado(const std::string& id);
    RespuestaHttp manejarExportarCsv(const PeticionHttp& peticion);

    std::string validarConfiguracion(const ConfiguracionCorrida& config,
                                     const ValorJson& objeto) const;

    Ejecutor&  m_ejecutor;
    Historial& m_historial;

    std::atomic<bool> m_parando{false};

    std::mutex  m_candadoCorrida;
    std::thread m_hiloCorrida;
};

}

#endif
