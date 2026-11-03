#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Laboratorio {

const ValorJson* ValorJson::buscar(const std::string& clave) const {
    if (tipo != Tipo::Objeto) return nullptr;
    auto it = campos.find(clave);
    if (it == campos.end()) return nullptr;
    return &it->second;
}

long ValorJson::entero(const std::string& clave, long porDefecto) const {
    const ValorJson* valor = buscar(clave);
    if (valor == nullptr || valor->tipo != Tipo::Numero) return porDefecto;
    return static_cast<long>(valor->valorNumero);
}

double ValorJson::real(const std::string& clave, double porDefecto) const {
    const ValorJson* valor = buscar(clave);
    if (valor == nullptr || valor->tipo != Tipo::Numero) return porDefecto;
    return valor->valorNumero;
}

bool ValorJson::booleano(const std::string& clave, bool porDefecto) const {
    const ValorJson* valor = buscar(clave);
    if (valor == nullptr || valor->tipo != Tipo::Booleano) return porDefecto;
    return valor->valorBooleano;
}

std::string ValorJson::texto(const std::string& clave, const std::string& porDefecto) const {
    const ValorJson* valor = buscar(clave);
    if (valor == nullptr || valor->tipo != Tipo::Texto) return porDefecto;
    return valor->valorTexto;
}

namespace {

void agregarNumero(std::string& salida, double valor) {
    if (!std::isfinite(valor)) {
        salida += "0";
        return;
    }
    char bufer[64];
    std::snprintf(bufer, sizeof(bufer), "%.10g", valor);
    salida += bufer;
}

}

void EscritorJson::separar() {

    if (m_trasClave) {
        m_trasClave = false;
        return;
    }
    if (m_pila.empty()) return;
    if (!m_pila.back()) m_salida.push_back(',');
    m_pila.back() = false;
}

void EscritorJson::escribirTexto(const std::string& valor) {
    m_salida.push_back('"');
    m_salida += escaparJson(valor);
    m_salida.push_back('"');
}

void EscritorJson::abrirObjeto() {
    separar();
    m_salida.push_back('{');
    m_pila.push_back(true);
}

void EscritorJson::cerrarObjeto() {
    m_salida.push_back('}');
    if (!m_pila.empty()) m_pila.pop_back();
}

void EscritorJson::abrirArreglo() {
    separar();
    m_salida.push_back('[');
    m_pila.push_back(true);
}

void EscritorJson::cerrarArreglo() {
    m_salida.push_back(']');
    if (!m_pila.empty()) m_pila.pop_back();
}

void EscritorJson::clave(const std::string& nombre) {
    separar();
    escribirTexto(nombre);
    m_salida.push_back(':');
    m_trasClave = true;
}

void EscritorJson::campo(const std::string& nombre, const std::string& valor) {
    clave(nombre);
    separar();
    escribirTexto(valor);
}

void EscritorJson::campo(const std::string& nombre, const char* valor) {
    campo(nombre, std::string(valor == nullptr ? "" : valor));
}

void EscritorJson::campo(const std::string& nombre, long valor) {
    clave(nombre);
    separar();
    char bufer[32];
    std::snprintf(bufer, sizeof(bufer), "%ld", valor);
    m_salida += bufer;
}

void EscritorJson::campo(const std::string& nombre, int valor) {
    campo(nombre, static_cast<long>(valor));
}

void EscritorJson::campo(const std::string& nombre, double valor) {
    clave(nombre);
    separar();
    agregarNumero(m_salida, valor);
}

void EscritorJson::campo(const std::string& nombre, bool valor) {
    clave(nombre);
    separar();
    m_salida += valor ? "true" : "false";
}

void EscritorJson::campoNulo(const std::string& nombre) {
    clave(nombre);
    separar();
    m_salida += "null";
}

void EscritorJson::campoCrudo(const std::string& nombre, const std::string& json) {
    clave(nombre);
    separar();
    m_salida += json;
}

void EscritorJson::elemento(const std::string& valor) {
    separar();
    escribirTexto(valor);
}

void EscritorJson::elemento(long valor) {
    separar();
    char bufer[32];
    std::snprintf(bufer, sizeof(bufer), "%ld", valor);
    m_salida += bufer;
}

void EscritorJson::elemento(double valor) {
    separar();
    agregarNumero(m_salida, valor);
}

void EscritorJson::elemento(bool valor) {
    separar();
    m_salida += valor ? "true" : "false";
}

void EscritorJson::elementoCrudo(const std::string& json) {
    separar();
    m_salida += json;
}

void EscritorJson::objetoVacio(const std::string& nombre) {
    clave(nombre);
    separar();
    m_salida += "{}";
}

void EscritorJson::arregloVacio(const std::string& nombre) {
    clave(nombre);
    separar();
    m_salida += "[]";
}

void EscritorJson::limpiar() {
    m_salida.clear();
    m_pila.clear();
    m_trasClave = false;
}

std::string escaparJson(const std::string& texto) {
    std::string salida;
    salida.reserve(texto.size() + 8);
    for (unsigned char caracter : texto) {
        switch (caracter) {
            case '"':  salida += "\\\""; break;
            case '\\': salida += "\\\\"; break;
            case '\b': salida += "\\b";  break;
            case '\f': salida += "\\f";  break;
            case '\n': salida += "\\n";  break;
            case '\r': salida += "\\r";  break;
            case '\t': salida += "\\t";  break;
            default:
                if (caracter < 0x20) {
                    char bufer[8];
                    std::snprintf(bufer, sizeof(bufer), "\\u%04x", caracter);
                    salida += bufer;
                } else {
                    salida.push_back(static_cast<char>(caracter));
                }
                break;
        }
    }
    return salida;
}

namespace {

class Analizador {
public:
    explicit Analizador(const std::string& documento) : m_documento(documento) {}

    std::optional<ValorJson> analizar() {
        saltarEspacios();
        auto valor = leerValor(0);
        if (!valor) return std::nullopt;
        saltarEspacios();
        if (m_posicion != m_documento.size()) return std::nullopt;
        return valor;
    }

private:
    static constexpr int kProfundidadMaxima = 32;

    void saltarEspacios() {
        while (m_posicion < m_documento.size()) {
            unsigned char caracter = static_cast<unsigned char>(m_documento[m_posicion]);
            if (caracter == ' ' || caracter == '\t' || caracter == '\n' || caracter == '\r') {
                ++m_posicion;
            } else {
                break;
            }
        }
    }

    bool consumir(char esperado) {
        if (m_posicion >= m_documento.size() || m_documento[m_posicion] != esperado) {
            return false;
        }
        ++m_posicion;
        return true;
    }

    std::optional<ValorJson> leerValor(int profundidad) {
        if (profundidad > kProfundidadMaxima) return std::nullopt;
        saltarEspacios();
        if (m_posicion >= m_documento.size()) return std::nullopt;

        char caracter = m_documento[m_posicion];
        switch (caracter) {
            case '{': return leerObjeto(profundidad);
            case '[': return leerArreglo(profundidad);
            case '"': {
                auto texto = leerTexto();
                if (!texto) return std::nullopt;
                ValorJson valor;
                valor.tipo = ValorJson::Tipo::Texto;
                valor.valorTexto = *texto;
                return valor;
            }
            case 't':
                if (coincide("true")) {
                    ValorJson valor;
                    valor.tipo = ValorJson::Tipo::Booleano;
                    valor.valorBooleano = true;
                    return valor;
                }
                return std::nullopt;
            case 'f':
                if (coincide("false")) {
                    ValorJson valor;
                    valor.tipo = ValorJson::Tipo::Booleano;
                    valor.valorBooleano = false;
                    return valor;
                }
                return std::nullopt;
            case 'n':
                if (coincide("null")) return ValorJson{};
                return std::nullopt;
            default:
                return leerNumero();
        }
    }

    bool coincide(const char* literal) {
        std::size_t largo = std::strlen(literal);
        if (m_documento.compare(m_posicion, largo, literal) != 0) return false;
        m_posicion += largo;
        return true;
    }

    std::optional<ValorJson> leerObjeto(int profundidad) {
        if (!consumir('{')) return std::nullopt;
        ValorJson valor;
        valor.tipo = ValorJson::Tipo::Objeto;

        saltarEspacios();
        if (consumir('}')) return valor;

        while (true) {
            saltarEspacios();
            auto clave = leerTexto();
            if (!clave) return std::nullopt;
            saltarEspacios();
            if (!consumir(':')) return std::nullopt;
            auto hijo = leerValor(profundidad + 1);
            if (!hijo) return std::nullopt;
            valor.campos[*clave] = std::move(*hijo);
            saltarEspacios();
            if (consumir('}')) return valor;
            if (!consumir(',')) return std::nullopt;
        }
    }

    std::optional<ValorJson> leerArreglo(int profundidad) {
        if (!consumir('[')) return std::nullopt;
        ValorJson valor;
        valor.tipo = ValorJson::Tipo::Arreglo;

        saltarEspacios();
        if (consumir(']')) return valor;

        while (true) {
            auto hijo = leerValor(profundidad + 1);
            if (!hijo) return std::nullopt;
            valor.elementos.push_back(std::move(*hijo));
            saltarEspacios();
            if (consumir(']')) return valor;
            if (!consumir(',')) return std::nullopt;
        }
    }

    std::optional<std::string> leerTexto() {
        if (!consumir('"')) return std::nullopt;
        std::string salida;
        while (m_posicion < m_documento.size()) {
            unsigned char caracter = static_cast<unsigned char>(m_documento[m_posicion]);
            if (caracter == '"') {
                ++m_posicion;
                return salida;
            }
            if (caracter == '\\') {
                ++m_posicion;
                if (m_posicion >= m_documento.size()) return std::nullopt;
                char escape = m_documento[m_posicion++];
                switch (escape) {
                    case '"':  salida.push_back('"');  break;
                    case '\\': salida.push_back('\\'); break;
                    case '/':  salida.push_back('/');  break;
                    case 'b':  salida.push_back('\b'); break;
                    case 'f':  salida.push_back('\f'); break;
                    case 'n':  salida.push_back('\n'); break;
                    case 'r':  salida.push_back('\r'); break;
                    case 't':  salida.push_back('\t'); break;
                    case 'u': {
                        unsigned int punto = 0;
                        for (int i = 0; i < 4; ++i) {
                            if (m_posicion >= m_documento.size()) return std::nullopt;
                            char digito = m_documento[m_posicion++];
                            punto <<= 4;
                            if (digito >= '0' && digito <= '9') punto |= static_cast<unsigned>(digito - '0');
                            else if (digito >= 'a' && digito <= 'f') punto |= static_cast<unsigned>(digito - 'a' + 10);
                            else if (digito >= 'A' && digito <= 'F') punto |= static_cast<unsigned>(digito - 'A' + 10);
                            else return std::nullopt;
                        }

                        if (punto < 0x80) {
                            salida.push_back(static_cast<char>(punto));
                        } else if (punto < 0x800) {
                            salida.push_back(static_cast<char>(0xC0 | (punto >> 6)));
                            salida.push_back(static_cast<char>(0x80 | (punto & 0x3F)));
                        } else {
                            salida.push_back(static_cast<char>(0xE0 | (punto >> 12)));
                            salida.push_back(static_cast<char>(0x80 | ((punto >> 6) & 0x3F)));
                            salida.push_back(static_cast<char>(0x80 | (punto & 0x3F)));
                        }
                        break;
                    }
                    default:
                        return std::nullopt;
                }
            } else {
                salida.push_back(static_cast<char>(caracter));
                ++m_posicion;
            }
        }
        return std::nullopt;
    }

    std::optional<ValorJson> leerNumero() {
        std::size_t inicio = m_posicion;
        if (m_posicion < m_documento.size() && m_documento[m_posicion] == '-') ++m_posicion;
        while (m_posicion < m_documento.size() &&
               std::isdigit(static_cast<unsigned char>(m_documento[m_posicion]))) {
            ++m_posicion;
        }
        if (m_posicion < m_documento.size() && m_documento[m_posicion] == '.') {
            ++m_posicion;
            while (m_posicion < m_documento.size() &&
                   std::isdigit(static_cast<unsigned char>(m_documento[m_posicion]))) {
                ++m_posicion;
            }
        }
        if (m_posicion < m_documento.size() &&
            (m_documento[m_posicion] == 'e' || m_documento[m_posicion] == 'E')) {
            ++m_posicion;
            if (m_posicion < m_documento.size() &&
                (m_documento[m_posicion] == '+' || m_documento[m_posicion] == '-')) {
                ++m_posicion;
            }
            while (m_posicion < m_documento.size() &&
                   std::isdigit(static_cast<unsigned char>(m_documento[m_posicion]))) {
                ++m_posicion;
            }
        }
        if (m_posicion == inicio) return std::nullopt;

        std::string trozo = m_documento.substr(inicio, m_posicion - inicio);
        char* fin = nullptr;
        double numero = std::strtod(trozo.c_str(), &fin);
        if (fin == nullptr || *fin != '\0') return std::nullopt;

        ValorJson valor;
        valor.tipo = ValorJson::Tipo::Numero;
        valor.valorNumero = numero;
        return valor;
    }

    const std::string& m_documento;
    std::size_t        m_posicion = 0;
};

}

std::optional<ValorJson> analizarJson(const std::string& documento) {
    Analizador analizador(documento);
    return analizador.analizar();
}

}
