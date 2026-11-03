#ifndef LABORATORIO_JSON_H
#define LABORATORIO_JSON_H

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Laboratorio {

struct ValorJson {
    enum class Tipo { Nulo, Booleano, Numero, Texto, Arreglo, Objeto };

    Tipo        tipo = Tipo::Nulo;
    bool        valorBooleano = false;
    double      valorNumero = 0.0;
    std::string valorTexto;
    std::vector<ValorJson> elementos;
    std::map<std::string, ValorJson> campos;

    bool esNulo() const { return tipo == Tipo::Nulo; }
    bool esObjeto() const { return tipo == Tipo::Objeto; }
    bool esArreglo() const { return tipo == Tipo::Arreglo; }

    const ValorJson* buscar(const std::string& clave) const;

    long        entero(const std::string& clave, long porDefecto) const;
    double      real(const std::string& clave, double porDefecto) const;
    bool        booleano(const std::string& clave, bool porDefecto) const;
    std::string texto(const std::string& clave, const std::string& porDefecto) const;
};

class EscritorJson {
public:
    void abrirObjeto();
    void cerrarObjeto();
    void abrirArreglo();
    void cerrarArreglo();

    void clave(const std::string& nombre);

    void campo(const std::string& nombre, const std::string& valor);
    void campo(const std::string& nombre, const char* valor);
    void campo(const std::string& nombre, long valor);
    void campo(const std::string& nombre, int valor);
    void campo(const std::string& nombre, double valor);
    void campo(const std::string& nombre, bool valor);
    void campoNulo(const std::string& nombre);

    void campoCrudo(const std::string& nombre, const std::string& json);

    void elemento(const std::string& valor);
    void elemento(long valor);
    void elemento(double valor);
    void elemento(bool valor);
    void elementoCrudo(const std::string& json);

    void objetoVacio(const std::string& nombre);
    void arregloVacio(const std::string& nombre);

    const std::string& texto() const { return m_salida; }
    void limpiar();

private:
    void separar();
    void escribirTexto(const std::string& valor);

    std::string        m_salida;
    std::vector<bool>  m_pila;
    bool               m_trasClave = false;
};

std::optional<ValorJson> analizarJson(const std::string& documento);

std::string escaparJson(const std::string& texto);

}

#endif
