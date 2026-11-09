#include "Escenarios.h"

namespace Laboratorio {

namespace {

VarianteEscenario variante(const std::string& etiqueta, const ConfiguracionCorrida& config) {
    VarianteEscenario resultado;
    resultado.etiqueta = etiqueta;
    resultado.config   = config;
    return resultado;
}

}

std::vector<Escenario> escenarios() {
    std::vector<Escenario> lista;

    {
        Escenario escenario;
        escenario.id          = "concurrencia";
        escenario.nombre      = "Prueba 1 · Concurrencia";
        escenario.descripcion = "Misma carga de memoria repartida entre 1, 2, 4 y 8 hilos nativos. "
                                "Sirve para ver cómo cambia la duración y el uso de CPU al aumentar "
                                "el paralelismo.";
        escenario.pasos = {
            "Elegir la variante de 1 hilo y ejecutarla.",
            "Repetir con 2, 4 y 8 hilos sin cambiar memoria ni pasadas.",
            "En Comparar, mirar duración, speedup y CPU promedio.",
            "Los fallos de página totales deben quedar aproximadamente constantes."
        };

        ConfiguracionCorrida base;
        base.carga       = ModoCarga::Memoria;
        base.modeloHilos = ModeloHilos::Nativos;
        base.hilos       = 1;
        base.memoriaMb   = 128;
        base.pasadas     = 20;
        base.modoFin     = ModoFin::Pasadas;
        escenario.base   = base;

        for (int hilos : {1, 2, 4, 8}) {
            ConfiguracionCorrida config = base;
            config.hilos                = hilos;
            escenario.variantes.push_back(
                variante(std::to_string(hilos) + (hilos == 1 ? " hilo" : " hilos"), config));
        }
        lista.push_back(escenario);
    }

    {
        Escenario escenario;
        escenario.id          = "limite-cpu";
        escenario.nombre      = "Prueba 2 · Límite de CPU";
        escenario.descripcion = "Tiempo fijo de 5 s con la CPU limitada al 100 %, 50 % y 25 %. "
                                "Se compara el trabajo completado. El límite es aproximado: se "
                                "aplica pausando y reanudando el proceso.";
        escenario.pasos = {
            "Ejecutar la variante de 100 % como referencia.",
            "Repetir con 50 % y con 25 %.",
            "Comparar pasadas completadas y CPU promedio.",
            "Opcional: probar el presupuesto RLIMIT_CPU con 3 s y duración 30 s."
        };

        ConfiguracionCorrida base;
        base.carga                 = ModoCarga::Memoria;
        base.modeloHilos           = ModeloHilos::Nativos;
        base.hilos                 = 1;
        base.memoriaMb             = 64;
        base.modoFin               = ModoFin::Duracion;
        base.duracionSeg           = 5;
        base.limiteCpuPorcentaje   = 100;
        escenario.base             = base;

        for (int porcentaje : {100, 50, 25}) {
            ConfiguracionCorrida config  = base;
            config.limiteCpuPorcentaje   = porcentaje;
            escenario.variantes.push_back(
                variante(std::to_string(porcentaje) + " % de CPU", config));
        }
        lista.push_back(escenario);
    }

    {
        Escenario escenario;
        escenario.id          = "presion-memoria";
        escenario.nombre      = "Prueba 3 · Presión de memoria";
        escenario.descripcion = "Se piden 64, 128 y 256 MB con un límite RLIMIT_AS de 192 MB. "
                                "Las dos primeras completan; la tercera choca contra el límite y "
                                "lo informa en vez de fallar.";
        escenario.pasos = {
            "Ejecutar la variante de 64 MB y anotar RSS y VA.",
            "Repetir con 128 MB.",
            "Ejecutar 256 MB: debe terminar en estado limite_memoria.",
            "Observar swap del proceso, swap del sistema y pswpin/pswpout."
        };

        ConfiguracionCorrida base;
        base.carga           = ModoCarga::Memoria;
        base.modeloHilos     = ModeloHilos::Nativos;
        base.hilos           = 2;
        base.memoriaMb       = 64;
        base.pasadas         = 10;
        base.modoFin         = ModoFin::Pasadas;
        base.limiteMemoriaMb = 192;
        escenario.base       = base;

        for (int memoria : {64, 128, 256}) {
            ConfiguracionCorrida config = base;
            config.memoriaMb            = memoria;
            escenario.variantes.push_back(
                variante(std::to_string(memoria) + " MB", config));
        }
        lista.push_back(escenario);
    }

    {
        Escenario escenario;
        escenario.id          = "sincronizacion";
        escenario.nombre      = "Prueba 4 · Sincronización";
        escenario.descripcion = "Cuatro hilos incrementan un contador compartido un millón de veces "
                                "cada uno, sin protección, con mutex y con operación atómica.";
        escenario.pasos = {
            "Ejecutar la variante sin sincronización y anotar actualizaciones perdidas.",
            "Repetir con mutex: el contador debe dar exacto y el tiempo ser el mayor.",
            "Repetir con atómico: contador exacto y tiempo intermedio.",
            "En Comparar, contrastar contador final y duración de los tres modos."
        };

        ConfiguracionCorrida base;
        base.carga          = ModoCarga::Sincronizacion;
        base.modeloHilos    = ModeloHilos::Nativos;
        base.hilos          = 4;
        base.iteraciones    = 1000000;
        base.sincronizacion = ModoSincronizacion::Ninguna;
        escenario.base      = base;

        for (ModoSincronizacion modo :
             {ModoSincronizacion::Ninguna, ModoSincronizacion::Mutex,
              ModoSincronizacion::Atomico}) {
            ConfiguracionCorrida config = base;
            config.sincronizacion       = modo;
            escenario.variantes.push_back(variante(textoSincronizacion(modo), config));
        }
        lista.push_back(escenario);
    }

    return lista;
}

std::optional<Escenario> buscarEscenario(const std::string& id) {
    for (const Escenario& escenario : escenarios()) {
        if (escenario.id == id) return escenario;
    }
    return std::nullopt;
}

std::string configuracionAJson(const ConfiguracionCorrida& config) {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.campo("carga", textoCarga(config.carga));
    escritor.campo("modeloHilos", textoModelo(config.modeloHilos));
    escritor.campo("hilos", config.hilos);
    escritor.campo("memoriaMb", config.memoriaMb);
    escritor.campo("pasadas", config.pasadas);
    escritor.campo("modoFin", textoModoFin(config.modoFin));
    escritor.campo("duracionSeg", config.duracionSeg);
    escritor.campo("sincronizacion", textoSincronizacion(config.sincronizacion));
    escritor.campo("iteraciones", config.iteraciones);
    escritor.campo("limiteMemoriaMb", config.limiteMemoriaMb);
    escritor.campo("limiteCpuSegundos", config.limiteCpuSegundos);
    escritor.campo("limiteCpuPorcentaje", config.limiteCpuPorcentaje);
    escritor.cerrarObjeto();
    return escritor.texto();
}

ConfiguracionCorrida configuracionDesdeJson(const ValorJson& objeto) {
    ConfiguracionCorrida config;

    config.carga       = cargaDesdeTexto(objeto.texto("carga", textoCarga(config.carga)));
    config.modeloHilos = modeloDesdeTexto(objeto.texto("modeloHilos", textoModelo(config.modeloHilos)));
    config.hilos       = static_cast<int>(objeto.entero("hilos", config.hilos));
    config.memoriaMb   = static_cast<int>(objeto.entero("memoriaMb", config.memoriaMb));
    config.pasadas     = static_cast<int>(objeto.entero("pasadas", config.pasadas));
    config.modoFin     = modoFinDesdeTexto(objeto.texto("modoFin", textoModoFin(config.modoFin)));
    config.duracionSeg = static_cast<int>(objeto.entero("duracionSeg", config.duracionSeg));
    config.sincronizacion =
        sincronizacionDesdeTexto(objeto.texto("sincronizacion", textoSincronizacion(config.sincronizacion)));
    config.iteraciones           = objeto.entero("iteraciones", config.iteraciones);
    config.limiteMemoriaMb       = static_cast<int>(objeto.entero("limiteMemoriaMb", config.limiteMemoriaMb));
    config.limiteCpuSegundos     = static_cast<int>(objeto.entero("limiteCpuSegundos", config.limiteCpuSegundos));
    config.limiteCpuPorcentaje   = static_cast<int>(objeto.entero("limiteCpuPorcentaje", config.limiteCpuPorcentaje));

    return config;
}

std::optional<ConfiguracionCorrida> configuracionDesdeTexto(const std::string& json) {
    std::optional<ValorJson> valor = analizarJson(json);
    if (!valor.has_value() || !valor->esObjeto()) return std::nullopt;
    return configuracionDesdeJson(*valor);
}

std::string escenariosAJson() {
    EscritorJson escritor;
    escritor.abrirObjeto();
    escritor.clave("escenarios");
    escritor.abrirArreglo();

    for (const Escenario& escenario : escenarios()) {
        escritor.abrirObjeto();
        escritor.campo("id", escenario.id);
        escritor.campo("nombre", escenario.nombre);
        escritor.campo("descripcion", escenario.descripcion);

        escritor.clave("pasos");
        escritor.abrirArreglo();
        for (const std::string& paso : escenario.pasos) escritor.elemento(paso);
        escritor.cerrarArreglo();

        escritor.campoCrudo("base", configuracionAJson(escenario.base));

        escritor.clave("variantes");
        escritor.abrirArreglo();
        for (const VarianteEscenario& varianteActual : escenario.variantes) {
            escritor.abrirObjeto();
            escritor.campo("etiqueta", varianteActual.etiqueta);
            escritor.campoCrudo("config", configuracionAJson(varianteActual.config));
            escritor.cerrarObjeto();
        }
        escritor.cerrarArreglo();

        escritor.cerrarObjeto();
    }

    escritor.cerrarArreglo();
    escritor.cerrarObjeto();
    return escritor.texto();
}

}
