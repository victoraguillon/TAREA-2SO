#ifndef LABORATORIO_ESCENARIOS_H
#define LABORATORIO_ESCENARIOS_H

#include <optional>
#include <string>
#include <vector>

#include "Json.h"
#include "Tipos.h"

namespace Laboratorio {

struct VarianteEscenario {
    std::string         etiqueta;
    ConfiguracionCorrida config;
};

struct Escenario {
    std::string                    id;
    std::string                    nombre;
    std::string                    descripcion;
    std::vector<std::string>       pasos;
    ConfiguracionCorrida           base;
    std::vector<VarianteEscenario> variantes;
};

std::vector<Escenario> escenarios();

std::optional<Escenario> buscarEscenario(const std::string& id);

std::string configuracionAJson(const ConfiguracionCorrida& config);

ConfiguracionCorrida configuracionDesdeJson(const ValorJson& objeto);

std::optional<ConfiguracionCorrida> configuracionDesdeTexto(const std::string& json);

std::string escenariosAJson();

}

#endif
