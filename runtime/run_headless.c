/* Runs the recompiled game without a window: the entry point, a number of frames, then a summary of the state.
     run <rom.gba> [frames] [--press KEY@FRAME[+LEN]]... [--shot file.ppm] [--shot-at FRAME:file.ppm]... [--dump-at FRAME]
     [--script file] (lines "KEY FRAME [LEN]") [--watch N] (every N frames: hashes of VRAM, OAM, EWRAM)
   Keys: a b select start right left up down r l. */
#include "gba_sys.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_entry(Cpu *c); /* generated: calls the function at the entry of the ROM */

static struct press { u32 key, from, to; } *presses;
static int n_presses, cap_presses;
static void add_press(u32 key, u32 from, u32 len) {
    if (n_presses == cap_presses) { cap_presses = cap_presses ? cap_presses * 2 : 256; presses = realloc(presses, cap_presses * sizeof *presses); }
    presses[n_presses].key = key; presses[n_presses].from = from; presses[n_presses].to = from + len; n_presses++;
}
static u32 watch_every;
#ifdef RT_ROMSTAT
#ifndef ROM_SHIFT
#define ROM_SHIFT 12
#endif
extern u32 rt_rom_stat[1 << 20]; extern int rt_rom_stat_on;
extern u32 rt_wtrace_val, rt_wtrace_left;
extern u32 rt_vtrace_lo, rt_vtrace_hi, rt_vtrace_left;
static u32 stat_from, stat_to;
static void rom_stat_report(void) {
    /* the 24 most read 4 KB pages of the ROM in the window */
    for (int n = 0; n < (getenv("ROMSTAT_N") ? atoi(getenv("ROMSTAT_N")) : 24); n++) {
        int best = -1; u32 bv = 0;
        for (int i = 0; i < (1 << 20); i++) if (rt_rom_stat[i] > bv) { bv = rt_rom_stat[i]; best = i; }
        if (best < 0) break;
        printf("romstat block 0x%06X reads %u\n", (unsigned)best << ROM_SHIFT, bv);
        rt_rom_stat[best] = 0;
    }
}
#endif
static struct { u32 frame; char path[260]; } shots[16];
static int n_shots;
static const char *final_shot;

static u32 hash(const uint8_t *p, u32 n) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static FILE *wav;
static u32 wav_frames, audio_peak;
static void wav_u32(u32 v) { fwrite(&v, 4, 1, wav); }
static void wav_open(const char *path) {
    wav = fopen(path, "wb");
    if (!wav) return;
    fwrite("RIFF", 1, 4, wav); wav_u32(0); fwrite("WAVEfmt ", 1, 8, wav); wav_u32(16);
    uint16_t fmt[2] = {1, 2}; fwrite(fmt, 2, 2, wav); wav_u32(32768); wav_u32(32768 * 4);
    uint16_t bl[2] = {4, 16}; fwrite(bl, 2, 2, wav); fwrite("data", 1, 4, wav); wav_u32(0);
}
static void wav_close(void) {
    if (!wav) return;
    u32 n = wav_frames * 4, r = 36 + n;
    fseek(wav, 4, SEEK_SET); fwrite(&r, 4, 1, wav);
    fseek(wav, 40, SEEK_SET); fwrite(&n, 4, 1, wav);
    fclose(wav); wav = NULL;
}

void sys_report(void) {
    wav_close();
    printf("audio: %u frames of 32768 Hz, peak %u
", wav_frames, audio_peak);
    u32 pal = 0;
    for (int i = 0; i < 512; i++) if (rt_pal[i * 2] | rt_pal[i * 2 + 1]) pal++;
    printf("frames %u  DISPCNT %04X  BG0CNT %04X  IE %04X  IME %u\n", sys_frames, io_get16(IO_DISPCNT), io_get16(0x08), io_get16(IO_IE), io_get16(IO_IME));
    printf("last site 0x%08X  (yield or run-time jump)\n", rt_site);
    printf("hash  vram %08X  pal %08X  oam %08X  ewram %08X  iwram %08X  (palette entries not 0: %u)\n",
           hash(rt_vram, sizeof rt_vram), hash(rt_pal, sizeof rt_pal), hash(rt_oam, sizeof rt_oam), hash(rt_ewram, sizeof rt_ewram), hash(rt_iwram, sizeof rt_iwram), pal);
    if (final_shot && ppu_write_ppm(final_shot)) printf("screenshot %s\n", final_shot);
}

static u32 dump_frame = ~0u;

static void dump_file(const char *name, const void *p, size_t n) {
    FILE *f = fopen(name, "wb");
    if (f) { fwrite(p, 1, n, f); fclose(f); }
}

/* the video registers on stdout and VRAM, palette, OAM and I/O in dump_*.bin */
static void dump_video(void) {
    printf("DISPCNT %04X  BGCNT %04X %04X %04X %04X  WIN0H %04X WIN0V %04X WININ %04X WINOUT %04X  BLDCNT %04X BLDALPHA %04X BLDY %04X MOSAIC %04X\n",
           io_get16(0), io_get16(8), io_get16(0xA), io_get16(0xC), io_get16(0xE), io_get16(0x40), io_get16(0x44), io_get16(0x48), io_get16(0x4A),
           io_get16(0x50), io_get16(0x52), io_get16(0x54), io_get16(0x4C));
    for (int i = 0; i < 4; i++) printf("BG%d scroll %u,%u\n", i, io_get16(0x10 + i * 4) & 0x1FF, io_get16(0x12 + i * 4) & 0x1FF);
    dump_file("dump_vram.bin", rt_vram, sizeof rt_vram);
    dump_file("dump_pal.bin", rt_pal, sizeof rt_pal);
    dump_file("dump_oam.bin", rt_oam, sizeof rt_oam);
    dump_file("dump_io.bin", rt_io, sizeof rt_io);
    int n = 0;
    for (int i = 0; i < 128; i++) { u32 a0 = rt_oam[i * 8] | rt_oam[i * 8 + 1] << 8; if ((a0 & 0x300) != 0x200) n++; }
    printf("visible sprites %d\n", n);
}

static struct { u32 frame; int what; char path[260]; } states[8];
static int n_states;

static void on_frame(u32 f) {
    u32 k = 0;
    for (int i = 0; i < n_states; i++) if (states[i].frame == f) { snprintf(rt_state_path, sizeof rt_state_path, "%s", states[i].path); rt_state_req = states[i].what; }
    if (rt_state_msg) { printf("frame %u: %s
", f, rt_state_msg); rt_state_msg = NULL; }
    for (u32 i = 0; i < apu_n * 2; i++) { u32 a = apu_out[i] < 0 ? -apu_out[i] : apu_out[i]; if (a > audio_peak) audio_peak = a; }
    if (wav) fwrite(apu_out, 4, apu_n, wav);
    wav_frames += apu_n; apu_n = 0;
    if (f == dump_frame) dump_video();
    for (int i = 0; i < n_presses; i++) if (f >= presses[i].from && f < presses[i].to) k |= presses[i].key;
#ifdef RT_ROMSTAT
#ifndef ROM_SHIFT
#define ROM_SHIFT 12
#endif
    if (stat_to && f == stat_from) { if (getenv("VTRACE")) { sscanf(getenv("VTRACE"), "%x:%x:%u", &rt_vtrace_lo, &rt_vtrace_hi, &rt_vtrace_left); rt_rom_stat_on = 1; } if (getenv("WTRACE")) { sscanf(getenv("WTRACE"), "%x:%u", &rt_wtrace_val, &rt_wtrace_left); rt_vtrace_left = 1; rt_vtrace_hi = 0; } memset(rt_rom_stat, 0, sizeof rt_rom_stat); rt_rom_stat_on = 1; }
    if (stat_to && f == stat_to) { rt_rom_stat_on = 0; rom_stat_report(); }
#endif
    if (watch_every && f % watch_every == 0) {
        /* a fingerprint of what is on the screen's sources, to see whether the game is alive: frame, vram, oam, ewram, IE */
        printf("watch %u vram %08X oam %08X ewram %08X IE %04X keys %03X\n", f, hash(rt_vram, sizeof rt_vram), hash(rt_oam, sizeof rt_oam), hash(rt_ewram, sizeof rt_ewram), io_get16(IO_IE), k);
        fflush(stdout);
    }
    sys_keys = k;
    for (int i = 0; i < n_shots; i++) if (shots[i].frame == f && ppu_write_ppm(shots[i].path)) printf("screenshot %s (frame %u)\n", shots[i].path, f);
}

static u32 key_bit(const char *n) {
    static const char *names[] = {"a", "b", "select", "start", "right", "left", "up", "down", "r", "l"};
    for (int i = 0; i < 10; i++) if (!strcmp(n, names[i])) return 1u << i;
    fprintf(stderr, "unknown key %s\n", n);
    exit(2);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: run <rom.gba> [frames] [--press KEY@FRAME[+LEN]] [--shot file.ppm] [--shot-at FRAME:file.ppm]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    rt_rom_len = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);
    rt_rom = malloc(rt_rom_len);
    if (fread(rt_rom, 1, rt_rom_len, f) != rt_rom_len) return 1;
    fclose(f);
    sys_max_frames = 60;
    for (int i = 2; i < argc; i++) {
        if ((!strcmp(argv[i], "--save-state") || !strcmp(argv[i], "--load-state")) && i + 1 < argc && n_states < 8) {
            states[n_states].what = argv[i][2] == 's' ? 1 : 2;
            if (sscanf(argv[++i], "%u:%259s", &states[n_states].frame, states[n_states].path) != 2) { fprintf(stderr, "bad --save-state/--load-state (FRAME:file)
"); return 2; }
            n_states++; continue;
        }
#ifdef RT_ROMSTAT
#ifndef ROM_SHIFT
#define ROM_SHIFT 12
#endif
        if (!strcmp(argv[i], "--romstat") && i + 1 < argc) { sscanf(argv[++i], "%u:%u", &stat_from, &stat_to); continue; }
#endif
        if (!strcmp(argv[i], "--watch") && i + 1 < argc) { watch_every = (u32)atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--script") && i + 1 < argc) {
            /* one press per line: KEY FRAME [LEN] */
            FILE *sf = fopen(argv[++i], "r");
            if (!sf) { perror(argv[i]); return 1; }
            char ln[128], name[16]; u32 from, len;
            while (fgets(ln, sizeof ln, sf)) {
                len = 4;
                if (sscanf(ln, "%15s %u %u", name, &from, &len) >= 2) add_press(key_bit(name), from, len);
            }
            fclose(sf); continue;
        }
        if (!strcmp(argv[i], "--wav") && i + 1 < argc) { wav_open(argv[++i]); continue; }
        if (!strcmp(argv[i], "--dump-at") && i + 1 < argc) { dump_frame = (u32)atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--press") && i + 1 < argc) {
            char name[16]; u32 from, len = 4;
            if (sscanf(argv[++i], "%15[a-z]@%u+%u", name, &from, &len) < 2) { fprintf(stderr, "bad --press\n"); return 2; }
            add_press(key_bit(name), from, len);
        } else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            final_shot = argv[++i];
        } else if (!strcmp(argv[i], "--shot-at") && i + 1 < argc) {
            u32 fr; char path[260];
            if (sscanf(argv[++i], "%u:%259s", &fr, path) != 2) { fprintf(stderr, "bad --shot-at\n"); return 2; }
            shots[n_shots].frame = fr; strcpy(shots[n_shots].path, path); n_shots++;
        } else if (argv[i][0] != '-') {
            sys_max_frames = (u32)atoi(argv[i]);
        }
    }
    {
        /* the save file goes next to the ROM: <rom without .gba>.sav */
        char sav[512];
        snprintf(sav, sizeof sav, "%s", argv[1]);
        char *dot = strrchr(sav, '.');
        if (dot && (!strchr(dot, '/')) && (!strchr(dot, 92))) *dot = 0;
        strncat(sav, ".sav", sizeof sav - strlen(sav) - 1);
        save_init(sav);
    }
    sys_frame_hook = on_frame;
    static Cpu cpu;
    rt_reset_armed = 1;
    setjmp(rt_reset_jb); /* SoftReset comes back here */
    sys_init(&cpu);
    rt_entry(&cpu);
    fprintf(stderr, "the entry point returned\n");
    sys_report();
    return 0;
}
