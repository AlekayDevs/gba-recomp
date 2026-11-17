/* Entrada de SDL en Android: arma los argumentos del frontal (runtime/play.c, compilado con -Dmain=gba_play_main). La ROM es
   <carpeta externa de la app>/rom.gba (se deja con `adb push`); las partidas y config.ini van a la misma carpeta. */
#include <SDL.h>
#include <stdio.h>

int gba_play_main(int argc, char **argv);

int SDL_main(int argc, char **argv) {
    (void)argc; (void)argv;
    static char rom[700], dir[600];
    const char *ext = SDL_AndroidGetExternalStoragePath();
    snprintf(dir, sizeof dir, "%s", ext ? ext : ".");
    snprintf(rom, sizeof rom, "%s/rom.gba", dir);
    char *av[] = {"play", rom, "--saves", dir, "--brand", "The Minish Cap", "--theme", "0F2A22,1F4A38,E8C547,F3E9C4", "--language", "es", NULL};
    return gba_play_main(10, av);
}
