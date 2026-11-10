/* Runs the recompiled game without a window: the entry point, a number of frames, then a summary of the state.
     run <rom.gba> [frames] [--press KEYS@FRAME...]   */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_entry(Cpu *c); /* generated: calls the function at the entry of the ROM */

static u32 hash(const uint8_t *p, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void sys_report(void) {
    u32 pal = 0;
    for (int i = 0; i < 512; i++) if (rt_pal[i * 2] | rt_pal[i * 2 + 1]) pal++;
    printf("frames %u  DISPCNT %04X  BG0CNT %04X  IE %04X  IME %u\n", sys_frames, io_get16(IO_DISPCNT), io_get16(0x08), io_get16(IO_IE), io_get16(IO_IME));
    printf("hash  vram %08X  pal %08X  oam %08X  ewram %08X  iwram %08X  (palette entries not 0: %u)\n",
           hash(rt_vram, sizeof rt_vram), hash(rt_pal, sizeof rt_pal), hash(rt_oam, sizeof rt_oam), hash(rt_ewram, sizeof rt_ewram), hash(rt_iwram, sizeof rt_iwram), pal);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: run <rom.gba> [frames]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    rt_rom_len = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);
    rt_rom = malloc(rt_rom_len);
    if (fread(rt_rom, 1, rt_rom_len, f) != rt_rom_len) return 1;
    fclose(f);
    sys_max_frames = argc > 2 ? (u32)atoi(argv[2]) : 60;
    static Cpu cpu;
    sys_init(&cpu);
    rt_entry(&cpu);
    fprintf(stderr, "the entry point returned\n");
    sys_report();
    return 0;
}
