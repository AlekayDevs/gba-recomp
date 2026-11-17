# Bibliotecas nativas de la app Android de gba-port:
#   libSDL2.so  SDL2 (fuentes hermanas, ver gradle.properties)
#   libtcc.so   TinyCC como biblioteca (LGPL 2.1), aparte para poder sustituirla
#   libmain.so  el runtime y el frontal de runtime/play.c (clang), el generador (Rust, estatico) y android_main.c, que genera el C del
#               juego desde la ROM, lo compila en memoria con libtcc y llama al frontal
cmake_minimum_required(VERSION 3.22.1)
project(gbaport C)

foreach(v GBA_RECOMP_DIR SDL2_DIR TINYCC_DIR GBA_GAME_DIR GBA_GEN_LIB)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "falta ${v} (lo pasa app/build.gradle)")
    endif()
    file(TO_CMAKE_PATH "${${v}}" ${v})
endforeach()
if(NOT EXISTS ${TINYCC_DIR}/libtcc.c)
    message(FATAL_ERROR "no estan las fuentes de TinyCC en ${TINYCC_DIR} (ver android/gradle.properties)")
endif()
if(EXISTS ${TINYCC_DIR}/config.h)
    message(FATAL_ERROR "${TINYCC_DIR}/config.h es de ./configure para el PC: usa una copia de TinyCC sin configurar")
endif()
if(NOT EXISTS ${GBA_GEN_LIB})
    message(FATAL_ERROR "no esta ${GBA_GEN_LIB}: la tarea cargoGenerator de Gradle lo construye (rustup target add aarch64-linux-android)")
endif()

add_link_options(-Wl,-z,max-page-size=16384)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_DIR} SDL2-build)

# libtcc (LGPL 2.1) como biblioteca aparte (libtcc.so)
add_library(tcc SHARED ${TINYCC_DIR}/libtcc.c)
target_include_directories(tcc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tcc_config ${TINYCC_DIR})
target_compile_definitions(tcc PRIVATE ONE_SOURCE=1 TCC_TARGET_ARM64=1 TARGETOS_ANDROID=1 _GNU_SOURCE)
target_compile_options(tcc PRIVATE -O2 -w -fPIC)

add_library(gbagen STATIC IMPORTED)
set_target_properties(gbagen PROPERTIES IMPORTED_LOCATION ${GBA_GEN_LIB})

set(RT ${GBA_RECOMP_DIR}/runtime)
add_library(main SHARED android_main.c ${RT}/play.c ${RT}/gba_cpu.c ${RT}/gba_bus.c ${RT}/gba_sys.c ${RT}/gba_ppu.c ${RT}/gba_apu.c
    ${RT}/gba_state.c ${RT}/gba_save.c ${RT}/gba_bios.c ${GBA_GAME_DIR}/text.c ${GBA_GAME_DIR}/options.c)
target_include_directories(main PRIVATE ${RT} ${SDL2_DIR}/include ${TINYCC_DIR})
target_compile_definitions(main PRIVATE RT_GAME RT_GAME_OPTS)
# play.c define main: lo renombramos para que la entrada sea SDL_main (android_main.c)
set_source_files_properties(${RT}/play.c PROPERTIES COMPILE_DEFINITIONS "main=gba_play_main")
target_compile_options(main PRIVATE -O2 -std=gnu11 -w -D_GNU_SOURCE)
target_link_libraries(main SDL2 tcc gbagen log dl m android)
