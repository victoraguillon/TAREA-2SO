#ifndef LABORATORIO_UTILES_H
#define LABORATORIO_UTILES_H

#include <cstddef>
#include <string>
#include <vector>

namespace Laboratorio {

class DescriptorArchivo {
public:
    DescriptorArchivo() = default;
    explicit DescriptorArchivo(int descriptor) : m_descriptor(descriptor) {}
    ~DescriptorArchivo();

    DescriptorArchivo(const DescriptorArchivo&) = delete;
    DescriptorArchivo& operator=(const DescriptorArchivo&) = delete;
    DescriptorArchivo(DescriptorArchivo&& otro) noexcept;
    DescriptorArchivo& operator=(DescriptorArchivo&& otro) noexcept;

    int  obtener() const { return m_descriptor; }
    bool valido() const { return m_descriptor >= 0; }
    int  liberar();
    void cerrar();

private:
    int m_descriptor = -1;
};

std::string formatearBytes(long kb);

std::string formatearDuracion(long ms);

long relojMonotonicoMs();

void dormirMilisegundos(long ms);

std::string fechaIso8601Local();

std::string idCorrida(const std::string& fecha, int secuencia);

std::string rutaEjecutable();

std::string rutaRaizProyecto();

bool leerArchivo(const std::string& ruta, std::string& contenido);

bool escribirTodo(int descriptor, const std::string& contenido);

std::vector<std::string> dividir(const std::string& texto, char separador);

std::string recortar(const std::string& texto);

std::string escaparCsv(const std::string& texto);

}

#endif
