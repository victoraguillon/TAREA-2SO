# Laboratorio de concurrencia y memoria

Aplicación de escritorio con interfaz web local que ejecuta experimentos
controlados de concurrencia y memoria sobre Linux y muestra cómo responde el
sistema operativo: memoria virtual (VA), memoria residente (RSS), swap, fallos
de página y CPU.

El binario es único y sin dependencias externas: C++17, CMake y `pthread`. La
interfaz se sirve desde el propio programa y se abre en el navegador; no usa
frameworks, CDN ni conexión a Internet.

---

## 1. Requisitos del entorno

- Linux (probado en WSL2 Ubuntu, kernel 6.18).
- `g++` con C++17, `cmake` ≥ 3.16, `pthread` y `ucontext`.
- No requiere `sudo` ni permisos especiales.
- Navegador en el mismo equipo. En WSL2, `localhost` se reenvía a Windows.

No hacen falta bibliotecas gráficas: la interfaz es HTML/CSS/JS servida por el
programa.

## 2. Compilar y ejecutar

```bash
cd TAREA-2/laboratorio
./build.sh        # compila en ~/laboratorio-build y deja build/laboratorio
./run.sh          # compila si hace falta y arranca en http://localhost:8080
```

`build.sh` compila fuera de `/mnt/c` porque OneDrive y el sistema de archivos de
Windows son lentos con CMake; después copia el binario a `build/laboratorio`.

Alternativas:

```bash
./build/laboratorio --puerto 8080 --host 127.0.0.1
./build/laboratorio --puerto 8080 --host 127.0.0.1 --abrir
```

Si el puerto está ocupado se prueban los nueve siguientes. `--abrir` intenta
lanzar el navegador de Windows y no es imprescindible: la URL se imprime siempre.

Para detener el servidor: `Ctrl+C` en la terminal. Si hay una corrida activa, se
detiene el trabajador, se guarda el resultado parcial y no quedan procesos
huérfanos.

## 3. Uso de la interfaz

La ventana tiene seis pestañas:

| Pestaña | Para qué sirve |
|---|---|
| **Configurar** | Elegir escenario o personalizar la corrida e iniciarla |
| **En vivo** | Métricas, progreso y tres gráficos durante la corrida |
| **Resultados** | Historial persistente, detalle de una corrida y exportación CSV |
| **Comparar** | Comparar dos o más corridas y ver avisos de comparabilidad |
| **Capacidades** | Qué mecanismos de límite ofrece este kernel y por qué |
| **Ayuda** | Resumen de métricas, límites y pruebas |

Botones principales: **Iniciar**, **Detener** (termina en menos de 3 s) y
**Repetir** (relanza la misma configuración).

### Parámetros

| Campo | Valores | Significado |
|---|---|---|
| `carga` | `memoria`, `sincronizacion` | Tipo de trabajo |
| `modeloHilos` | `nativos`, `tareas` | Hilos 1:1 del SO o tareas cooperativas de runtime |
| `hilos` | 1..32 (según núcleos) | Cantidad de hilos o de tareas |
| `memoriaMb` | 1..4096 | Memoria a reservar y tocar |
| `pasadas` | 1..1000 | Pasadas completas sobre la memoria |
| `modoFin` | `pasadas`, `duracion` | Terminar por trabajo o por tiempo |
| `duracionSeg` | 1..180 | Duración objetivo en modo `duracion` |
| `sincronizacion` | `ninguna`, `mutex`, `atomico` | Acceso al contador compartido |
| `iteraciones` | 1..100000000 | Iteraciones por hilo en la carga de sincronización |
| `limiteMemoriaMb` | 0 u 8..4096 | `RLIMIT_AS` del trabajador (0 = sin límite) |
| `limiteCpuSegundos` | 0..600 | `RLIMIT_CPU` en segundos de CPU (0 = sin límite) |
| `limiteCpuPorcentaje` | 1..100 | Cuota aproximada de CPU por `SIGSTOP`/`SIGCONT` |

La API valida los rangos y rechaza con HTTP 400 y mensaje en español. Además, la
memoria pedida debe dejar al menos 1 GB libre para el sistema, contando las
pilas de los hilos. El máximo absoluto es 4096 MB.

## 4. Qué mide cada métrica

| Métrica | Origen | Interpretación |
|---|---|---|
| **VA** (`VmPeak`/`VmSize`) | `/proc/<pid>/status` | Espacio de direcciones reservado; no implica RAM usada |
| **RSS** (`VmHWM`/`VmRSS`) | `/proc/<pid>/status` | Páginas realmente residentes en RAM |
| **Swap del proceso** (`VmSwap`) | `/proc/<pid>/status` | Páginas del proceso llevadas a swap |
| **Swap del sistema** | `/proc/meminfo`, `/proc/vmstat` | Total/libre y movimiento (`pswpin`/`pswpout`) durante la corrida |
| **Fallos de página** | `/proc/<pid>/stat` | Menores (sin I/O) y mayores (con I/O) |
| **CPU** | `utime + stime` | Porcentaje del total de un núcleo; puede superar 100 % con varios hilos |
| **Hilos del SO** | `Threads` | Hilos reales del proceso |
| **Cambios de contexto** | `/proc/<pid>/status` | Voluntarios, involuntarios y de tareas de usuario |

El muestreo lo hace el supervisor cada 250 ms (primera muestra a los 100 ms).
El trabajador informa además sus picos finales, de modo que una corrida muy
corta también deja métricas.

## 5. Modelo de ejecución

El mismo binario cumple dos papeles:

```
supervisor (servidor HTTP + interfaz)
   └─ fork/exec ──► trabajador (--trabajador '<json>')
                       ├─ aplica y verifica límites
                       ├─ reserva y toca memoria / compite por el contador
                       └─ informa NDJSON por stdout
```

- El supervisor nunca aplica los límites a sí mismo: los aplica al hijo.
- El trabajador comprueba `/proc/<pid>/limits` y, si un límite pedido no quedó
  aplicado, sale con código 120 y la corrida se informa como error.
- `modeloHilos = nativos`: el hilo principal hace la rebanada 0 y se crean N−1
  hilos `pthread`, por lo que `/proc` muestra exactamente N hilos.
- `modeloHilos = tareas`: N contextos `ucontext` cooperativos sobre **un** hilo
  del SO; se ven `Threads: 1` y miles de cambios de contexto de usuario.
- `mallopt(M_ARENA_MAX, 1)` evita las arenas de malloc por hilo (64 MB de VA
  cada una), que no forman parte del experimento.

Códigos de salida del trabajador: `0` completada, `1` error interno, `2` límite
de memoria, `3` detenida, `4` límite de CPU, `120` límites no aplicados,
`121` fallo de `setrlimit`, `127` fallo de `exec`.

## 6. Límites: qué es exacto y qué es aproximado

| Mecanismo | Estado | Cómo se aplica |
|---|---|---|
| `RLIMIT_AS` | **exacto y verificado** | Límite de espacio de direcciones del hijo |
| `RLIMIT_CPU` | **exacto y verificado** | El kernel envía `SIGXCPU` al agotarse |
| Cuota de CPU por porcentaje | **aproximado** | `SIGSTOP`/`SIGCONT` periódicos del supervisor |
| cgroup v2 | **no disponible en este entorno** | Requiere permisos de escritura que no existen aquí |

La interfaz marca los `RLIMIT` como «✓ verificados» y la cuota de CPU como
«aplicado (aproximado)». Si un límite no se pudo aplicar, la corrida no se
presenta como limitada: se informa el error.

El swap se muestra si el sistema lo tiene. En WSL2 el swap es un archivo del
host; puede no moverse aunque haya presión, y por eso se informan también
`pswpin`/`pswpout` y la presión de memoria (PSI) cuando está disponible.

## 7. Las cuatro pruebas

Los escenarios de la pestaña **Configurar** precargan estas pruebas:

1. **Concurrencia (P1)**: misma carga (128 MB, 20 pasadas), 1, 2, 4 y 8 hilos.
   Se espera que la duración baje y que la CPU supere el 100 % al crecer N.
2. **Límite de CPU (P2)**: 64 MB durante 5 s con cuota 100 %, 50 % y 25 %.
   Se espera que las pasadas completadas bajen con la cuota.
3. **Presión de memoria (P3)**: pedir 256 MB con `RLIMIT_AS` de 192 MB.
   Se espera estado `limite_memoria`, reserva parcial (~176 MB) y límite
   verificado; con 128 MB bajo el mismo límite, la corrida completa.
4. **Sincronización (P4)**: 4 hilos × 1 000 000 iteraciones con `ninguna`,
   `mutex` y `atomico`. Se esperan actualizaciones perdidas solo en `ninguna`.

Resultados observados en este equipo (16 núcleos, 7,6 GB de RAM, 2 GB de swap):

| Prueba | Resultado |
|---|---|
| P1 1/2/4/8 hilos | 6046 / 3024 / 1588 / 884 ms; CPU 99 / 196 / 385 / 739 % |
| P2 100/50/25 % | 30,4 / 15,0 / 6,9 pasadas en 5 s; CPU 99,3 / 49,3 / 24,6 % |
| P3 256 MB con límite 192 MB | `limite_memoria`, reservado 176 MB, VA 187 MB |
| P4 ninguna / mutex / atómico | 2 726 564 pérdidas / 0 / 0 |
| Tareas 4 × 128 MB × 20 pasadas | `Threads: 1`, 5124 cambios de contexto, 6537 ms |

## 8. Historial, CSV y comparación

- Cada corrida se agrega como una línea JSON a `resultados/historial.jsonl`.
- El historial sobrevive al reinicio del programa.
- `GET /api/resultados` devuelve un resumen sin muestras; el detalle completo
  está en `GET /api/resultados/<id>`.
- **Exportar CSV** descarga todas las corridas, o solo las seleccionadas con
  `?ids=...`.
- **Comparar** avisa cuando las corridas no son estrictamente comparables
  (distinta carga, modelo, memoria, pasadas o límites).

## 9. API HTTP

| Método y ruta | Descripción |
|---|---|
| `GET /api/sistema` | Núcleos, RAM, swap, PSI y disponibilidad de cgroup |
| `GET /api/capacidades` | Mecanismos de límite y su estado |
| `GET /api/escenarios` | Escenarios y variantes precargadas |
| `POST /api/corrida/iniciar` | Inicia una corrida (JSON de configuración) |
| `POST /api/corrida/detener` | Detiene la corrida activa |
| `GET /api/corrida/estado` | Estado actual |
| `GET /api/eventos` | Flujo SSE: `estado`, `muestra`, `fin` |
| `GET /api/resultados` | Resumen del historial, más reciente primero |
| `GET /api/resultados/<id>` | Detalle con muestras |
| `GET /api/resultados/exportar.csv` | Exportación CSV |

Ejemplo:

```bash
curl -s -X POST localhost:8080/api/corrida/iniciar \
  -H 'Content-Type: application/json' \
  -d '{"carga":"memoria","modeloHilos":"nativos","hilos":4,
       "memoriaMb":128,"pasadas":20,"modoFin":"pasadas"}'
```

## 10. Estructura del proyecto

```
laboratorio/
├── CMakeLists.txt
├── build.sh / run.sh
├── docs/
│   ├── DISENO.md        # diseño y checklist de aceptación
│   └── PROMPT.md
├── src/
│   ├── main.cpp         # supervisor o trabajador
│   ├── Api.*            # rutas HTTP y validación
│   ├── ServidorHttp.*   # HTTP/1.1, estáticos y SSE
│   ├── Ejecutor.*       # fork/exec, muestreo, límites, resultado
│   ├── Trabajador.*     # carga real y métricas finales
│   ├── Tareas.*         # planificador cooperativo con ucontext
│   ├── Limites.*        # RLIMIT y cuota de CPU
│   ├── LectorProc.*     # /proc/<pid>/{status,stat}, meminfo, vmstat
│   ├── Capacidades.*    # detección de cgroup, PSI, swap
│   ├── Escenarios.*     # las cuatro pruebas y variantes
│   ├── Historial.*      # JSONL, CSV y consultas
│   ├── Json.*           # lectura y escritura JSON
│   ├── Tipos.h          # configuración, resultado y muestra
│   └── Utiles.*         # reloj, fechas, rutas, señales
├── web/
│   ├── index.html
│   ├── estilos.css
│   └── app.js
└── resultados/
    └── historial.jsonl
```

## 11. Notas para la revisión

- La revisión se hace en este mismo entorno WSL2; el binario ya compilado está
  en `build/laboratorio`.
- No se necesita `sudo`. La cuota de CPU por porcentaje es aproximada por
  diseño; los `RLIMIT` sí son exactos y se verifican contra `/proc`.
- cgroup v2 aparece como **no disponible** porque el contenedor WSL no permite
  escribir en el árbol de cgroup; no se simula.
- Si `build.sh` falla con `Text file busy`, hay un servidor en ejecución:
  deténgalo con `Ctrl+C` o `kill $(cat /tmp/lab.pid)`.
- El historial se crea en la primera corrida y puede borrarse sin afectar al
  programa.
