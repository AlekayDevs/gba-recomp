# Bibliotecas nativas de la prueba Android de gba-port: libSDL2.so y libmain.so (frontal de runtime/play.c + runtime + el C del juego).
cmake_minimum_required(VERSION 3.22.1)
project(gbaport C)

foreach(v GBA_RECOMP_DIR SDL2_DIR GBA_GEN_C GBA_GAME_DIR)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "falta ${v} (lo pasa app/build.gradle)")
    endif()
    file(TO_CMAKE_PATH "${${v}}" ${v})
endforeach()
if(NOT EXISTS ${GBA_GEN_C})
    message(FATAL_ERROR "no esta ${GBA_GEN_C}: generalo con `gba-recomp gen`")
endif()

add_link_options(-Wl,-z,max-page-size=16384)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${SDL2_DIR} SDL2-build)

set(RT ${GBA_RECOMP_DIR}/runtime)
add_library(main SHARED android_main.c ${RT}/play.c ${RT}/gba_cpu.c ${RT}/gba_bus.c ${RT}/gba_sys.c ${RT}/gba_ppu.c ${RT}/gba_apu.c
    ${RT}/gba_state.c ${RT}/gba_save.c ${RT}/gba_bios.c ${GBA_GEN_C} ${GBA_GAME_DIR}/text.c ${GBA_GAME_DIR}/options.c)
target_include_directories(main PRIVATE ${RT} ${SDL2_DIR}/include)
target_compile_definitions(main PRIVATE RT_GAME RT_GAME_OPTS)
# play.c define main: lo renombramos para que la entrada sea SDL_main (android_main.c)
set_source_files_properties(${RT}/play.c PROPERTIES COMPILE_DEFINITIONS "main=gba_play_main")
target_compile_options(main PRIVATE -O1 -std=gnu11 -w -D_GNU_SOURCE)
target_link_libraries(main SDL2 log dl m android)
