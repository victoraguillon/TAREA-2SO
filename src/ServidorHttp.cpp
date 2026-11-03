#include "ServidorHttp.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "Json.h"
#include "Utiles.h"

namespace Laboratorio {

namespace {

constexpr std::size_t MAX_CABECERAS = 64 * 1024;

constexpr std::size_t MAX_CUERPO = 1024 * 1024;

constexpr int ESPERA_RECEPCION_SEGUNDOS = 5;
constexpr int ESPERA_ENVIO_SEGUNDOS     = 15;

std::string textoCodigo(int codigo) {
    switch (codigo) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

std::string tipoPorExtension(const std::string& ruta) {
    const std::size_t punto = ruta.find_last_of('.');
    if (punto == std::string::npos) return "application/octet-stream";

    const std::string extension = ruta.substr(punto + 1);
    if (extension == "html") return "text/html; charset=utf-8";
    if (extension == "css")  return "text/css; charset=utf-8";
    if (extension == "js")   return "application/javascript; charset=utf-8";
    if (extension == "json") return "application/json; charset=utf-8";
    if (extension == "svg")  return "image/svg+xml";
    if (extension == "png")  return "image/png";
    if (extension == "ico")  return "image/x-icon";
    return "application/octet-stream";
}

RespuestaHttp errorJson(int codigo, const std::string& mensaje) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("error", mensaje);
    escritor.cerrarObjeto();

    RespuestaHttp respuesta;
    respuesta.codigo = codigo;
    respuesta.cuerpo = escritor.texto();
    return respuesta;
}

}

ServidorHttp::ServidorHttp(std::string host, int puerto,
                           std::string directorioWeb)
    : m_host(std::move(host)),
      m_puerto(puerto),
      m_directorioWeb(std::move(directorioWeb)) {}

ServidorHttp::~ServidorHttp() {
    detener();
}

bool ServidorHttp::iniciar(std::string& error) {
    const int puertoInicial = m_puerto;

    for (int intento = 0; intento < 10; ++intento) {
        const int puerto = puertoInicial + intento;

        const int descriptor = socket(AF_INET, SOCK_STREAM, 0);
        if (descriptor < 0) {
            error = "No se pudo crear el socket del servidor.";
            return false;
        }

        int uno = 1;
        setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));

        sockaddr_in direccion{};
        direccion.sin_family = AF_INET;
        direccion.sin_port   = htons(static_cast<std::uint16_t>(puerto));

        if (m_host.empty() || m_host == "0.0.0.0") {
            direccion.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (inet_pton(AF_INET, m_host.c_str(), &direccion.sin_addr) != 1) {
            ::close(descriptor);
            error = "Dirección de escucha inválida: " + m_host;
            return false;
        }

        if (bind(descriptor, reinterpret_cast<sockaddr*>(&direccion),
                 sizeof(direccion)) == 0 &&
            listen(descriptor, 64) == 0) {
            m_socketEscucha = descriptor;
            m_puerto        = puerto;
            return true;
        }

        ::close(descriptor);
    }

    error = "No se encontró un puerto libre entre " +
            std::to_string(puertoInicial) + " y " +
            std::to_string(puertoInicial + 9) + ".";
    return false;
}

std::string ServidorHttp::url() const {
    return "http://localhost:" + std::to_string(m_puerto);
}

void ServidorHttp::servir(const ComprobacionParada& parada) {
    m_activo.store(true);

    while (m_activo.load()) {
        if (parada && parada()) break;

        pollfd espera{};
        espera.fd     = m_socketEscucha;
        espera.events = POLLIN;

        const int listo = poll(&espera, 1, 250);
        if (listo < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (listo == 0) continue;

        const int cliente = accept(m_socketEscucha, nullptr, nullptr);
        if (cliente < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;
        }

        try {
            std::lock_guard<std::mutex> bloqueo(m_candadoHilos);
            m_hilos.emplace_back(&ServidorHttp::atenderConexion, this, cliente);
        } catch (const std::system_error&) {
            ::close(cliente);
        }
    }

    m_activo.store(false);
}

void ServidorHttp::detener() {
    m_activo.store(false);

    if (m_socketEscucha >= 0) {

        ::shutdown(m_socketEscucha, SHUT_RDWR);
        ::close(m_socketEscucha);
        m_socketEscucha = -1;
    }

    std::vector<std::thread> hilos;
    {
        std::lock_guard<std::mutex> bloqueo(m_candadoHilos);
        hilos.swap(m_hilos);
    }

    for (std::thread& hilo : hilos) {
        if (hilo.joinable()) hilo.join();
    }
}

void ServidorHttp::atenderConexion(int descriptor) {
    DescriptorArchivo conexion(descriptor);

    timeval esperaRecepcion{};
    esperaRecepcion.tv_sec = ESPERA_RECEPCION_SEGUNDOS;
    setsockopt(conexion.obtener(), SOL_SOCKET, SO_RCVTIMEO, &esperaRecepcion,
               sizeof(esperaRecepcion));

    timeval esperaEnvio{};
    esperaEnvio.tv_sec = ESPERA_ENVIO_SEGUNDOS;
    setsockopt(conexion.obtener(), SOL_SOCKET, SO_SNDTIMEO, &esperaEnvio,
               sizeof(esperaEnvio));

    PeticionHttp peticion;
    int codigoError = 0;
    if (!leerPeticion(conexion.obtener(), peticion, codigoError)) {
        enviarRespuesta(conexion.obtener(),
                        errorJson(codigoError, "Petición HTTP inválida."));
        return;
    }

    if (peticion.metodo != "GET" && peticion.metodo != "POST") {
        enviarRespuesta(conexion.obtener(),
                        errorJson(405, "Método no permitido."));
        return;
    }

    RespuestaHttp respuesta;
    if (peticion.ruta.rfind("/api/", 0) == 0) {
        if (!m_manejador) {
            respuesta = errorJson(500, "La API no está inicializada.");
        } else {
            respuesta = m_manejador(peticion);
        }
    } else {
        respuesta = responderEstatico(peticion);
    }

    enviarRespuesta(conexion.obtener(), respuesta);
}

bool ServidorHttp::leerPeticion(int descriptor, PeticionHttp& peticion,
                                int& codigoError) {
    codigoError = 400;

    std::string datos;
    std::size_t finCabecera = std::string::npos;
    char        bufer[4096];

    while (finCabecera == std::string::npos) {
        if (datos.size() > MAX_CABECERAS) {
            codigoError = 413;
            return false;
        }

        const ssize_t leidos = recv(descriptor, bufer, sizeof(bufer), 0);
        if (leidos < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (leidos == 0) return false;

        datos.append(bufer, static_cast<std::size_t>(leidos));
        finCabecera = datos.find("\r\n\r\n");
    }

    const std::string cabecera = datos.substr(0, finCabecera);
    std::string       cuerpo   = datos.substr(finCabecera + 4);

    const std::vector<std::string> lineas = dividir(cabecera, '\n');
    if (lineas.empty()) return false;

    const std::vector<std::string> partes = dividir(recortar(lineas.front()), ' ');
    if (partes.size() < 3) return false;

    peticion.metodo = partes[0];

    const std::string destino = partes[1];
    const std::size_t interrogacion = destino.find('?');
    if (interrogacion == std::string::npos) {
        peticion.ruta = destino;
    } else {
        peticion.ruta     = destino.substr(0, interrogacion);
        peticion.consulta = destino.substr(interrogacion + 1);
    }

    for (std::size_t indice = 1; indice < lineas.size(); ++indice) {
        const std::string linea = recortar(lineas[indice]);
        if (linea.empty()) continue;

        const std::size_t dosPuntos = linea.find(':');
        if (dosPuntos == std::string::npos) continue;

        std::string nombre = recortar(linea.substr(0, dosPuntos));
        for (char& caracter : nombre) {
            if (caracter >= 'A' && caracter <= 'Z') {
                caracter = static_cast<char>(caracter - 'A' + 'a');
            }
        }
        peticion.cabeceras[nombre] = recortar(linea.substr(dosPuntos + 1));
    }

    long longitud = 0;
    const auto encontrada = peticion.cabeceras.find("content-length");
    if (encontrada != peticion.cabeceras.end()) {
        char* fin = nullptr;
        errno = 0;
        longitud = std::strtol(encontrada->second.c_str(), &fin, 10);
        if (errno != 0 || fin == encontrada->second.c_str() || longitud < 0) {
            return false;
        }
    }

    if (static_cast<std::size_t>(longitud) > MAX_CUERPO) {
        codigoError = 413;
        return false;
    }

    while (cuerpo.size() < static_cast<std::size_t>(longitud)) {
        const ssize_t leidos = recv(descriptor, bufer, sizeof(bufer), 0);
        if (leidos < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (leidos == 0) return false;
        cuerpo.append(bufer, static_cast<std::size_t>(leidos));
    }
    cuerpo.resize(static_cast<std::size_t>(longitud));
    peticion.cuerpo = std::move(cuerpo);

    codigoError = 0;
    return true;
}

bool ServidorHttp::rutaSegura(const std::string& ruta) {
    if (ruta.empty() || ruta[0] != '/') return false;
    if (ruta.find('\\') != std::string::npos) return false;
    if (ruta.find('%') != std::string::npos) return false;
    if (ruta.find('\0') != std::string::npos) return false;

    for (const std::string& parte : dividir(ruta, '/')) {
        if (parte == "..") return false;
    }
    return true;
}

RespuestaHttp ServidorHttp::responderEstatico(const PeticionHttp& peticion) {
    std::string ruta = peticion.ruta;
    if (ruta == "/") ruta = "/index.html";

    if (!rutaSegura(ruta)) {
        return errorJson(404, "Ruta no encontrada.");
    }

    const std::string completa = m_directorioWeb + ruta;
    std::string       contenido;
    if (!leerArchivo(completa, contenido)) {
        return errorJson(404, "Ruta no encontrada.");
    }

    RespuestaHttp respuesta;
    respuesta.codigo        = 200;
    respuesta.tipoContenido = tipoPorExtension(ruta);
    respuesta.cuerpo        = std::move(contenido);
    return respuesta;
}

void ServidorHttp::enviarRespuesta(int descriptor,
                                   const RespuestaHttp& respuesta) {
    std::string cabecera = "HTTP/1.1 " + std::to_string(respuesta.codigo) +
                           " " + textoCodigo(respuesta.codigo) + "\r\n";
    cabecera += "Content-Type: " + respuesta.tipoContenido + "\r\n";
    if (respuesta.cabeceras.find("Cache-Control") == respuesta.cabeceras.end()) {
        cabecera += "Cache-Control: no-store\r\n";
    }
    cabecera += "X-Content-Type-Options: nosniff\r\n";

    for (const auto& par : respuesta.cabeceras) {
        cabecera += par.first + ": " + par.second + "\r\n";
    }

    if (respuesta.flujo) {

        cabecera += "Connection: keep-alive\r\n\r\n";
        if (!escribirTodo(descriptor, cabecera)) return;
        respuesta.flujo(descriptor);
        return;
    }

    cabecera += "Content-Length: " + std::to_string(respuesta.cuerpo.size()) + "\r\n";
    cabecera += "Connection: close\r\n\r\n";

    if (!escribirTodo(descriptor, cabecera)) return;
    if (!respuesta.cuerpo.empty()) {
        escribirTodo(descriptor, respuesta.cuerpo);
    }
}

}
