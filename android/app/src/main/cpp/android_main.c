/* Entrada de SDL en Android. Con la ROM que dejo LauncherActivity en <carpeta externa>/rom.gba: genera el C del juego con la
   biblioteca del generador (Rust), lo compila en memoria con libtcc, lo une al runtime (que va en esta misma biblioteca) y llama al
   frontal de runtime/play.c. El perfil, sus raices y las cabeceras para TinyCC los copia GameActivity a files/assets antes de
   empezar. No contiene nada de ninguna ROM. */
#define _GNU_SOURCE
#include <SDL.h>
#include <android/log.h>
#include <dirent.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "gba_rt.h"
#include "libtcc.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "gbaport", __VA_ARGS__)

int gba_play_main(int argc, char **argv);
int gba_gen_generate(const uint8_t *rom, size_t rom_len, const char *profile, const char *roots, const char *out_path, char *msg, size_t msg_len);

/* El C generado define estas dos con otro nombre (-Drt_entry=gen_entry ...); el runtime las llama por aqui. */
static void (*gen_entry)(Cpu *);
static void (*gen_dispatch)(Cpu *, u32);
void rt_entry(Cpu *c) { gen_entry(c); }
void rt_dispatch(Cpu *c, u32 t) { gen_dispatch(c, t); }

/* Lo que el C generado usa del runtime */
extern void rt_untranslated(Cpu *c, u32 addr);
#define SYM(x) {#x, (void *)&x}
static const struct { const char *name; void *addr; } rt_symbols[] = {
    SYM(rd8), SYM(rd16), SYM(rd32), SYM(rdsb), SYM(rdsh), SYM(wr8), SYM(wr16), SYM(wr32), SYM(rt_budget), SYM(rt_site), SYM(rt_state_req),
    SYM(rt_state_point), SYM(rt_mrs), SYM(rt_msr), SYM(rt_mrs_spsr), SYM(rt_msr_spsr), SYM(rt_swi), SYM(rt_yield), SYM(rt_missing),
    SYM(rt_untranslated), {NULL, NULL}};

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6;
}

/* El aviso de progreso de GameActivity.setProgress (null lo quita). */
static void progress(const char *text) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) return;
    jclass cls = (*env)->GetObjectClass(env, act);
    jmethodID m = (*env)->GetMethodID(env, cls, "setProgress", "(Ljava/lang/String;)V");
    if (m) {
        jstring js = text ? (*env)->NewStringUTF(env, text) : NULL;
        (*env)->CallVoidMethod(env, act, m, js);
        if (js) (*env)->DeleteLocalRef(env, js);
    }
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
}

static void fail(const char *msg) {
    progress(NULL);
    LOG("ERROR: %s", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Minish Cap", msg, NULL);
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    buf[n] = 0;
    if (len) *len = (size_t)n;
    return buf;
}

static void on_tcc_error(void *opaque, const char *msg) {
    (void)opaque;
    LOG("tcc: %s", msg);
}

int SDL_main(int argc, char **argv) {
    (void)argc; (void)argv;
    static char rom_path[700], dir[600], path[800], assets[700], gen_c[700], include[700], msg[512];
    const char *ext = SDL_AndroidGetExternalStoragePath(), *files = SDL_AndroidGetInternalStoragePath();
    snprintf(dir, sizeof dir, "%s", ext ? ext : ".");
    snprintf(rom_path, sizeof rom_path, "%s/rom.gba", dir);
    snprintf(assets, sizeof assets, "%s/assets", files ? files : ".");
    snprintf(include, sizeof include, "%s/tcc_include", assets);
    snprintf(gen_c, sizeof gen_c, "%s/gen.c", files ? files : ".");

    size_t rom_len = 0;
    char *rom = read_file(rom_path, &rom_len);
    snprintf(path, sizeof path, "%s/game/profile.toml", assets);
    char *toml = read_file(path, NULL);
    snprintf(path, sizeof path, "%s/game/roots.txt", assets);
    char *roots = read_file(path, NULL);
    if (!rom || !toml || !roots) { fail("Faltan la ROM o los ficheros del juego de la app."); return 1; }

    progress("gen");
    double t0 = now_ms();
    int rc = gba_gen_generate((const uint8_t *)rom, rom_len, toml, roots, gen_c, msg, sizeof msg);
    double t1 = now_ms();
    free(rom); free(toml); free(roots);
    if (rc) { snprintf(path, sizeof path, "Esta ROM no sirve: %s", msg); fail(path); return 1; }
    LOG("generado: %s", msg);

    progress("compile");
    TCCState *s = tcc_new();
    tcc_set_error_func(s, NULL, on_tcc_error);
    tcc_set_options(s, "-nostdinc -nostdlib");
    tcc_set_output_type(s, TCC_OUTPUT_MEMORY);
    tcc_add_sysinclude_path(s, include);
    tcc_define_symbol(s, "rt_entry", "gen_entry");
    tcc_define_symbol(s, "rt_dispatch", "gen_dispatch");
    for (int i = 0; rt_symbols[i].name; i++) tcc_add_symbol(s, rt_symbols[i].name, rt_symbols[i].addr);
    if (tcc_add_file(s, gen_c) < 0) { remove(gen_c); fail("El C del juego no compila (ver logcat)."); return 1; }
    remove(gen_c);
    if (tcc_relocate(s) < 0) { fail("No se pudo enlazar el juego (¿memoria ejecutable denegada?)."); return 1; }
    double t2 = now_ms();
    *(void **)&gen_entry = tcc_get_symbol(s, "gen_entry");
    *(void **)&gen_dispatch = tcc_get_symbol(s, "gen_dispatch");
    if (!gen_entry || !gen_dispatch) { fail("El juego generado no tiene punto de entrada."); return 1; }
    LOG("preparado: generar %.0f ms, compilar y enlazar %.0f ms", t1 - t0, t2 - t1);
    progress(NULL);

    char *av[] = {"play", rom_path, "--saves", dir, "--brand", "The Minish Cap", "--theme", "0F2A22,1F4A38,E8C547,F3E9C4", "--language", "es", NULL};
    return gba_play_main(10, av);
}
