cmake_minimum_required(VERSION 3.16)
project(Laboratorio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilacion" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(laboratorio
    src/main.cpp
    src/Utiles.cpp
    src/Json.cpp
    src/LectorProc.cpp
    src/Capacidades.cpp
    src/Limites.cpp
    src/Trabajador.cpp
    src/Tareas.cpp
    src/Ejecutor.cpp
    src/Historial.cpp
    src/Escenarios.cpp
    src/ServidorHttp.cpp
    src/Api.cpp
)

target_include_directories(laboratorio PRIVATE src)
target_compile_options(laboratorio PRIVATE -O2 -pthread)
target_link_options(laboratorio PRIVATE -pthread)
target_link_libraries(laboratorio PRIVATE Threads::Threads)
