/* Regresion: un estado truncado debe rechazarse sin modificar la maquina.
   Compilar como bios_test.c, sustituyendo bios_test.c por state_test.c.
   Ejecutar en una carpeta temporal: crea state-test-*.sst. No necesita ROM. */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sys_report(void) {}
void rt_dispatch(Cpu *c, u32 t) { (void)c; (void)t; }
static int total, fails;
#define CHECK(c) do { total++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static void request(Cpu *c, int req, const char *path) {
    snprintf(rt_state_path, sizeof rt_state_path, "%s", path);
    rt_state_req = req;
    rt_state_point(c);
}
static unsigned char *read_file(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) exit(2);
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); rewind(f);
    unsigned char *p = malloc(*n);
    if (!p || fread(p, 1, *n, f) != *n) exit(2);
    fclose(f); return p;
}
int main(void) {
    Cpu c = {0};
    rt_rom_len = 0xC0; rt_rom = calloc(1, rt_rom_len);
    if (!rt_rom) return 2;
    memset(rt_ewram, 0x11, sizeof rt_ewram); c.r[0] = 0x11111111;
    request(&c, 1, "state-test-old.sst");
    CHECK(!strcmp(rt_state_msg, "state saved"));
    size_t old_n, current_n;
    unsigned char *old = read_file("state-test-old.sst", &old_n);
    memset(rt_ewram, 0x22, sizeof rt_ewram); c.r[0] = 0x22222222;
    request(&c, 1, "state-test-current.sst");
    unsigned char *current = read_file("state-test-current.sst", &current_n);
    const size_t cuts[] = {8 + 32 + sizeof(Cpu) + 64, 8 + 32 + sizeof(Cpu) + sizeof rt_ewram + 16, old_n - 1};
    for (unsigned i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
        request(&c, 2, "state-test-current.sst");
        CHECK(!strcmp(rt_state_msg, "state loaded"));
        FILE *f = fopen("state-test-bad.sst", "wb");
        if (!f) return 2;
        if (fwrite(old, 1, cuts[i], f) != cuts[i]) return 2;
        fclose(f);
        request(&c, 2, "state-test-bad.sst");
        CHECK(!strcmp(rt_state_msg, "the state file is damaged"));
        CHECK(c.r[0] == 0x22222222);
        request(&c, 1, "state-test-after.sst");
        size_t after_n;
        unsigned char *after = read_file("state-test-after.sst", &after_n);
        CHECK(after_n == current_n && !memcmp(after, current, current_n));
        free(after);
    }
    request(&c, 2, "state-test-old.sst");
    CHECK(!strcmp(rt_state_msg, "state loaded"));
    CHECK(c.r[0] == 0x11111111 && rt_ewram[0] == 0x11 && rt_ewram[sizeof rt_ewram - 1] == 0x11);
    free(old); free(current); free(rt_rom);
    printf("%d checks, %d failed\n", total, fails);
    return fails ? 1 : 0;
}
