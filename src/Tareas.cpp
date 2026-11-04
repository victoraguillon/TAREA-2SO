#include "Tareas.h"

#include <sys/mman.h>
#include <unistd.h>

namespace Laboratorio {

namespace {

constexpr std::size_t TAM_PILA_TAREA = 256 * 1024;

thread_local PlanificadorTareas* planificadorActual = nullptr;
thread_local int                 indiceActual       = -1;

}

void cederTarea() {
    if (planificadorActual == nullptr) return;
    planificadorActual->ceder();
}

void PlanificadorTareas::entradaTarea() {
    PlanificadorTareas* planificador = planificadorActual;
    if (planificador == nullptr) return;

    int indice = indiceActual;
    if (planificador->m_cuerpo != nullptr) {
        (*planificador->m_cuerpo)(indice);
    }
    if (indice >= 0 && indice < static_cast<int>(planificador->m_tareas.size())) {
        planificador->m_tareas[static_cast<std::size_t>(indice)].terminada = true;
    }

}

PlanificadorTareas::~PlanificadorTareas() {
    liberarPilas();
}

void PlanificadorTareas::liberarPilas() {
    for (Tarea& tarea : m_tareas) {
        if (tarea.base != nullptr) {
            munmap(tarea.base, tarea.bytes);
            tarea.base  = nullptr;
            tarea.bytes = 0;
        }
    }
    m_tareas.clear();
}

bool PlanificadorTareas::ejecutar(int cantidad, const std::function<void(int)>& cuerpo) {
    liberarPilas();
    if (cantidad <= 0) return true;

    long pagina = sysconf(_SC_PAGESIZE);
    if (pagina <= 0) pagina = 4096;

    m_tareas.resize(static_cast<std::size_t>(cantidad));
    m_cuerpo            = &cuerpo;
    m_cambiosDeContexto = 0;
    m_actual            = -1;

    for (int indice = 0; indice < cantidad; ++indice) {
        Tarea& tarea = m_tareas[static_cast<std::size_t>(indice)];
        std::size_t total = TAM_PILA_TAREA + static_cast<std::size_t>(pagina);

        void* base = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (base == MAP_FAILED) {
            liberarPilas();
            m_cuerpo = nullptr;
            return false;
        }
        tarea.base  = base;
        tarea.bytes = total;

        mprotect(base, static_cast<std::size_t>(pagina), PROT_NONE);

        if (getcontext(&tarea.contexto) != 0) {
            liberarPilas();
            m_cuerpo = nullptr;
            return false;
        }
        tarea.contexto.uc_stack.ss_sp   = static_cast<char*>(base) + pagina;
        tarea.contexto.uc_stack.ss_size = TAM_PILA_TAREA;
        tarea.contexto.uc_link          = &m_principal;
        makecontext(&tarea.contexto,
                    reinterpret_cast<void (*)()>(&PlanificadorTareas::entradaTarea), 0);
    }

    getcontext(&m_principal);
    planificadorActual = this;
    m_enEjecucion      = true;

    bool quedan = true;
    while (quedan) {
        quedan = false;
        for (int indice = 0; indice < cantidad; ++indice) {
            Tarea& tarea = m_tareas[static_cast<std::size_t>(indice)];
            if (tarea.terminada) continue;

            quedan     = true;
            m_actual   = indice;
            indiceActual = indice;
            ++m_cambiosDeContexto;
            swapcontext(&m_principal, &tarea.contexto);
        }
    }

    m_actual           = -1;
    indiceActual       = -1;
    m_enEjecucion      = false;
    planificadorActual = nullptr;
    m_cuerpo           = nullptr;

    liberarPilas();
    return true;
}

void PlanificadorTareas::ceder() {
    if (!m_enEjecucion || m_actual < 0) return;
    ++m_cambiosDeContexto;
    swapcontext(&m_tareas[static_cast<std::size_t>(m_actual)].contexto, &m_principal);
}

}
