#include "Utiles.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace Laboratorio {

DescriptorArchivo::~DescriptorArchivo() {
    cerrar();
}

DescriptorArchivo::DescriptorArchivo(DescriptorArchivo&& otro) noexcept
    : m_descriptor(otro.m_descriptor) {
    otro.m_descriptor = -1;
}

DescriptorArchivo& DescriptorArchivo::operator=(DescriptorArchivo&& otro) noexcept {
    if (this != &otro) {
        cerrar();
        m_descriptor = otro.m_descriptor;
        otro.m_descriptor = -1;
    }
    return *this;
}

int DescriptorArchivo::liberar() {
    int descriptor = m_descriptor;
    m_descriptor = -1;
    return descriptor;
}

void DescriptorArchivo::cerrar() {
    if (m_descriptor >= 0) {
        ::close(m_descriptor);
        m_descriptor = -1;
    }
}

std::string formatearBytes(long kb) {
    char bufer[64];
    double valor;
    const char* unidad;

    if (kb < 1024) {
        std::snprintf(bufer, sizeof(bufer), "%ld kB", kb);
        return bufer;
    }
    if (kb < 1024L * 1024L) {
        valor = static_cast<double>(kb) / 1024.0;
        unidad = "MB";
        std::snprintf(bufer, sizeof(bufer), "%.1f %s", valor, unidad);
    } else {
        valor = static_cast<double>(kb) / (1024.0 * 1024.0);
        unidad = "GB";
        std::snprintf(bufer, sizeof(bufer), "%.2f %s", valor, unidad);
    }
    std::string texto(bufer);
    for (char& caracter : texto) {
        if (caracter == '.') caracter = ',';
    }
    return texto;
}

std::string formatearDuracion(long ms) {
    char bufer[64];
    std::snprintf(bufer, sizeof(bufer), "%.2f s", static_cast<double>(ms) / 1000.0);
    std::string texto(bufer);
    for (char& caracter : texto) {
        if (caracter == '.') caracter = ',';
    }
    return texto;
}

long relojMonotonicoMs() {
    struct timespec ahora{};
    clock_gettime(CLOCK_MONOTONIC, &ahora);
    return static_cast<long>(ahora.tv_sec) * 1000L + ahora.tv_nsec / 1000000L;
}

void dormirMilisegundos(long ms) {
    if (ms <= 0) return;
    struct timespec peticion{};
    peticion.tv_sec = ms / 1000;
    peticion.tv_nsec = (ms % 1000) * 1000000L;

    while (nanosleep(&peticion, &peticion) != 0 && errno == EINTR) {
    }
}

std::string fechaIso8601Local() {
    std::time_t ahora = std::time(nullptr);
    std::tm partes{};
    localtime_r(&ahora, &partes);
    char bufer[32];
    std::strftime(bufer, sizeof(bufer), "%Y-%m-%dT%H:%M:%S", &partes);
    return bufer;
}

std::string idCorrida(const std::string& fecha, int secuencia) {

    std::string compacta;
    compacta.reserve(fecha.size());
    for (char caracter : fecha) {
        if (caracter == '-' || caracter == ':') continue;
        if (caracter == 'T') {
            compacta.push_back('-');
        } else {
            compacta.push_back(caracter);
        }
    }
    char sufijo[16];
    std::snprintf(sufijo, sizeof(sufijo), "-%03d", secuencia);
    return compacta + sufijo;
}

std::string rutaEjecutable() {
    char bufer[PATH_MAX];
    ssize_t leidos = readlink("/proc/self/exe", bufer, sizeof(bufer) - 1);
    if (leidos <= 0) return std::string();
    bufer[leidos] = '\0';
    return bufer;
}

std::string rutaRaizProyecto() {
    std::string ejecutable = rutaEjecutable();
    if (ejecutable.empty()) return std::string(".");

    std::string directorio = ejecutable;
    std::size_t barra = directorio.find_last_of('/');
    if (barra != std::string::npos) directorio = directorio.substr(0, barra);

    for (int nivel = 0; nivel < 6; ++nivel) {
        std::string candidato = directorio + "/web/index.html";
        if (access(candidato.c_str(), R_OK) == 0) return directorio;

        std::size_t posicion = directorio.find_last_of('/');
        if (posicion == std::string::npos || posicion == 0) break;
        directorio = directorio.substr(0, posicion);
    }
    return std::string(".");
}

bool leerArchivo(const std::string& ruta, std::string& contenido) {
    std::ifstream archivo(ruta, std::ios::binary);
    if (!archivo) return false;
    std::ostringstream acumulador;
    acumulador << archivo.rdbuf();
    contenido = acumulador.str();
    return true;
}

bool escribirTodo(int descriptor, const std::string& contenido) {
    std::size_t enviado = 0;
    while (enviado < contenido.size()) {
        ssize_t escritos = ::write(descriptor, contenido.data() + enviado,
                                   contenido.size() - enviado);
        if (escritos < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (escritos == 0) return false;
        enviado += static_cast<std::size_t>(escritos);
    }
    return true;
}

std::vector<std::string> dividir(const std::string& texto, char separador) {
    std::vector<std::string> partes;
    std::string actual;
    for (char caracter : texto) {
        if (caracter == separador) {
            partes.push_back(actual);
            actual.clear();
        } else {
            actual.push_back(caracter);
        }
    }
    partes.push_back(actual);
    return partes;
}

std::string recortar(const std::string& texto) {
    const char* espacios = " \t\r\n";
    std::size_t inicio = texto.find_first_not_of(espacios);
    if (inicio == std::string::npos) return std::string();
    std::size_t fin = texto.find_last_not_of(espacios);
    return texto.substr(inicio, fin - inicio + 1);
}

std::string escaparCsv(const std::string& texto) {
    bool necesitaComillas = false;
    for (char caracter : texto) {
        if (caracter == ',' || caracter == '"' || caracter == '\n' || caracter == '\r') {
            necesitaComillas = true;
            break;
        }
    }
    if (!necesitaComillas) return texto;

    std::string escapado;
    escapado.reserve(texto.size() + 2);
    escapado.push_back('"');
    for (char caracter : texto) {
        if (caracter == '"') escapado.push_back('"');
        escapado.push_back(caracter);
    }
    escapado.push_back('"');
    return escapado;
}

}
