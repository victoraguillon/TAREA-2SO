#ifndef LABORATORIO_SERVIDORHTTP_H
#define LABORATORIO_SERVIDORHTTP_H

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Laboratorio {

struct PeticionHttp {
    std::string metodo;
    std::string ruta;
    std::string consulta;
    std::map<std::string, std::string> cabeceras;
    std::string cuerpo;
};

struct RespuestaHttp {
    int         codigo         = 200;
    std::string tipoContenido  = "application/json; charset=utf-8";
    std::string cuerpo;
    std::map<std::string, std::string> cabeceras;
    std::function<void(int)> flujo;
};

class ServidorHttp {
public:
    using Manejador          = std::function<RespuestaHttp(const PeticionHttp&)>;
    using ComprobacionParada = std::function<bool()>;

    ServidorHttp(std::string host, int puerto, std::string directorioWeb);
    ~ServidorHttp();

    ServidorHttp(const ServidorHttp&) = delete;
    ServidorHttp& operator=(const ServidorHttp&) = delete;

    bool iniciar(std::string& error);

    void servir(const ComprobacionParada& parada);

    void detener();

    void establecerManejador(Manejador manejador) { m_manejador = std::move(manejador); }

    int         puerto() const { return m_puerto; }
    std::string url() const;

private:
    void atenderConexion(int descriptor);
    bool leerPeticion(int descriptor, PeticionHttp& peticion, int& codigoError);
    RespuestaHttp responderEstatico(const PeticionHttp& peticion);
    void enviarRespuesta(int descriptor, const RespuestaHttp& respuesta);

    static bool rutaSegura(const std::string& ruta);

    std::string m_host;
    int         m_puerto;
    std::string m_directorioWeb;

    int               m_socketEscucha = -1;
    std::atomic<bool> m_activo{false};
    Manejador         m_manejador;

    std::mutex               m_candadoHilos;
    std::vector<std::thread> m_hilos;
};

}

#endif
