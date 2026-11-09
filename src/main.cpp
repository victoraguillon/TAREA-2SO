#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "Api.h"
#include "Ejecutor.h"
#include "Escenarios.h"
#include "Historial.h"
#include "ServidorHttp.h"
#include "Trabajador.h"
#include "Utiles.h"

namespace {

volatile sig_atomic_t g_parar = 0;

void manejarSenal(int  ) {
    g_parar = 1;
}

void instalarManejadores() {
    struct sigaction accion;
    std::memset(&accion, 0, sizeof(accion));
    accion.sa_handler = manejarSenal;
    sigemptyset(&accion.sa_mask);
    accion.sa_flags = SA_RESTART;

    sigaction(SIGINT, &accion, nullptr);
    sigaction(SIGTERM, &accion, nullptr);

    signal(SIGPIPE, SIG_IGN);
}

void abrirNavegador(const std::string& url) {
    const pid_t pid = fork();
    if (pid == 0) {
        execlp("cmd.exe", "cmd.exe", "/c", "start", "", url.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }
    if (pid > 0) {
        int estado = 0;
        waitpid(pid, &estado, 0);
    }
}

void imprimirAyuda() {
    std::printf(
        "Laboratorio de concurrencia y memoria\n"
        "\n"
        "Uso:\n"
        "  laboratorio [--puerto N] [--host DIRECCION] [--web DIRECTORIO] [--abrir]\n"
        "  laboratorio --trabajador '<json>'\n"
        "\n"
        "  --puerto N       puerto de la interfaz (por defecto 8080; si está\n"
        "                   ocupado se prueban los nueve siguientes)\n"
        "  --host DIRECCION interfaz de escucha (por defecto 0.0.0.0)\n"
        "  --web DIRECTORIO carpeta con index.html (por defecto, la del proyecto)\n"
        "  --abrir          intenta abrir la interfaz en el navegador\n"
        "\n"
        "El modo --trabajador lo usa el propio programa: no está pensado para\n"
        "lanzarse a mano.\n");
}

}

int main(int argc, char** argv) {

    signal(SIGPIPE, SIG_IGN);

    if (argc >= 2 && std::strcmp(argv[1], "--trabajador") == 0) {
        if (argc < 3) {
            std::fprintf(stderr, "Falta la configuración del trabajador.\n");
            return 1;
        }
        std::optional<Laboratorio::ConfiguracionCorrida> config =
            Laboratorio::configuracionDesdeTexto(argv[2]);
        if (!config.has_value()) {
            std::fprintf(stderr, "Configuración del trabajador inválida.\n");
            return 1;
        }
        return Laboratorio::ejecutarTrabajador(*config);
    }

    int         puerto = 8080;
    std::string host   = "0.0.0.0";
    std::string web;
    bool        abrir = false;

    for (int indice = 1; indice < argc; ++indice) {
        const std::string argumento = argv[indice];

        if (argumento == "--ayuda" || argumento == "--help" || argumento == "-h") {
            imprimirAyuda();
            return 0;
        }
        if (argumento == "--puerto" && indice + 1 < argc) {
            puerto = std::atoi(argv[++indice]);
        } else if (argumento == "--host" && indice + 1 < argc) {
            host = argv[++indice];
        } else if (argumento == "--web" && indice + 1 < argc) {
            web = argv[++indice];
        } else if (argumento == "--abrir") {
            abrir = true;
        } else {
            std::fprintf(stderr, "Argumento desconocido: %s\n\n", argumento.c_str());
            imprimirAyuda();
            return 2;
        }
    }

    if (puerto < 1 || puerto > 65535) {
        std::fprintf(stderr, "Puerto inválido: %d\n", puerto);
        return 2;
    }

    if (web.empty()) {
        web = Laboratorio::rutaRaizProyecto() + "/web";
    }

    instalarManejadores();

    Laboratorio::ServidorHttp servidor(host, puerto, web);

    std::string error;
    if (!servidor.iniciar(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    Laboratorio::Historial historial;
    Laboratorio::Ejecutor  ejecutor(historial);
    Laboratorio::Api       api(ejecutor, historial);

    servidor.establecerManejador(
        [&api](const Laboratorio::PeticionHttp& peticion) {
            return api.manejar(peticion);
        });

    std::printf("Laboratorio de concurrencia y memoria\n");
    std::printf("Interfaz web: %s\n", servidor.url().c_str());
    std::printf("Historial:    %s\n", historial.ruta().c_str());
    std::printf("Ctrl+C para detener y salir.\n");
    std::fflush(stdout);

    if (abrir) abrirNavegador(servidor.url());

    servidor.servir([] { return g_parar != 0; });

    if (g_parar != 0) {
        std::printf("\nCerrando; se detiene la corrida activa si la hay.\n");
        std::fflush(stdout);
    }

    api.detener();
    servidor.detener();

    return 0;
}
