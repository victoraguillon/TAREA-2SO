#ifndef LABORATORIO_HISTORIAL_H
#define LABORATORIO_HISTORIAL_H

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "Json.h"
#include "Tipos.h"

namespace Laboratorio {

std::string muestraAJson(const Muestra& muestra, const std::string& tipo = "");

std::optional<Muestra> muestraDesdeJson(const ValorJson& objeto);

std::string resultadoAJson(const ResultadoCorrida& resultado,
                           const std::string& tipo = "");

std::optional<ResultadoCorrida> resultadoDesdeJson(const ValorJson& objeto);

class Historial {
public:

    explicit Historial(const std::string& rutaArchivo = "");

    bool agregar(const ResultadoCorrida& resultado);

    std::vector<ResultadoCorrida> cargarTodas();

    std::size_t cantidad();

    std::optional<ResultadoCorrida> buscar(const std::string& id);

    std::string exportarCsv(const std::vector<std::string>& ids);

    const std::string& ruta() const { return m_rutaArchivo; }

private:

    void cargarSiHaceFalta();

    std::string                   m_rutaArchivo;
    std::vector<ResultadoCorrida> m_cache;
    bool                          m_cargado = false;
    std::mutex                    m_candado;
};

}

#endif
