#ifndef LABORATORIO_TAREAS_H
#define LABORATORIO_TAREAS_H

#include <cstddef>
#include <functional>
#include <vector>

#include <ucontext.h>

namespace Laboratorio {

void cederTarea();

class PlanificadorTareas {
public:
    PlanificadorTareas() = default;
    ~PlanificadorTareas();

    PlanificadorTareas(const PlanificadorTareas&) = delete;
    PlanificadorTareas& operator=(const PlanificadorTareas&) = delete;

    bool ejecutar(int cantidad, const std::function<void(int)>& cuerpo);

    long cambiosDeContexto() const { return m_cambiosDeContexto; }

private:
    friend void cederTarea();
    static void entradaTarea();

    void ceder();
    void liberarPilas();

    struct Tarea {
        ucontext_t contexto{};
        void*      base       = nullptr;
        std::size_t bytes     = 0;
        bool       terminada  = false;
    };

    std::vector<Tarea>              m_tareas;
    ucontext_t                      m_principal{};
    const std::function<void(int)>* m_cuerpo            = nullptr;
    long                            m_cambiosDeContexto = 0;
    int                             m_actual            = -1;
    bool                            m_enEjecucion       = false;
};

}

#endif
