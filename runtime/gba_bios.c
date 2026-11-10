/* The BIOS by HLE (the official one is not distributed): the SWI calls that games use. Arguments are in r0-r3 of `c`; the generated code
   stores the registers before calling and reloads them after. */
#include "gba_sys.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIOS_IF 0x03007FF8

static void cpu_set(Cpu *c, int fast) {
    u32 src = c->r[0], dst = c->r[1], ctl = c->r[2];
    u32 n = ctl & 0x1FFFFF, fill = (ctl >> 24) & 1, w32 = fast ? 1 : (ctl >> 26) & 1;
    if (fast) n = (n + 7) & ~7u;
    u32 v = 0;
    if (fill) v = w32 ? rd32(src) : rd16(src);
    for (u32 i = 0; i < n; i++) {
        if (w32) { wr32(dst, fill ? v : rd32(src)); dst += 4; } else { wr16(dst, fill ? v : rd16(src)); dst += 2; }
        if (!fill) src += w32 ? 4 : 2;
    }
}

/* decompressors: the output is built in a buffer and then written, bytes (WRAM) or halfwords (VRAM) */
static void put_out(u32 dst, const uint8_t *buf, u32 n, int vram) {
    if (vram) for (u32 i = 0; i < n; i += 2) wr16(dst + i, buf[i] | ((i + 1 < n ? buf[i + 1] : 0) << 8));
    else for (u32 i = 0; i < n; i++) wr8(dst + i, buf[i]);
}

static void lz77(Cpu *c, int vram) {
    u32 src = c->r[0], dst = c->r[1];
    u32 hdr = rd32(src);
    u32 size = hdr >> 8;
    src += 4;
    uint8_t *out = malloc(size + 8);
    u32 n = 0;
    while (n < size) {
        u32 flags = rd8(src++);
        for (int i = 0; i < 8 && n < size; i++, flags <<= 1) {
            if (flags & 0x80) {
                u32 b1 = rd8(src++), b2 = rd8(src++);
                u32 len = (b1 >> 4) + 3, disp = (((b1 & 15) << 8) | b2) + 1;
                for (u32 k = 0; k < len && n < size; k++, n++) out[n] = n >= disp ? out[n - disp] : 0;
            } else {
                out[n++] = (uint8_t)rd8(src++);
            }
        }
    }
    put_out(dst, out, size, vram);
    c->r[0] = src; c->r[1] = dst + size;
    free(out);
}

static void rl(Cpu *c, int vram) {
    u32 src = c->r[0], dst = c->r[1];
    u32 size = rd32(src) >> 8;
    src += 4;
    uint8_t *out = malloc(size + 0x90);
    u32 n = 0;
    while (n < size) {
        u32 f = rd8(src++);
        if (f & 0x80) { u32 len = (f & 0x7F) + 3, b = rd8(src++); while (len-- && n < size) out[n++] = (uint8_t)b; }
        else { u32 len = (f & 0x7F) + 1; while (len-- && n < size) out[n++] = (uint8_t)rd8(src++); }
    }
    put_out(dst, out, size, vram);
    free(out);
}

static void diff_unfilter(Cpu *c, int bits16, int vram) {
    u32 src = c->r[0], dst = c->r[1];
    u32 hdr = rd32(src), size = hdr >> 8;
    src += 4;
    uint8_t *out = malloc(size + 8);
    if (bits16) {
        u32 acc = 0;
        for (u32 i = 0; i + 1 < size + 1; i += 2) { acc = (acc + rd16(src + i)) & 0xFFFF; out[i] = (uint8_t)acc; if (i + 1 < size + 8) out[i + 1] = (uint8_t)(acc >> 8); }
    } else {
        u32 acc = 0;
        for (u32 i = 0; i < size; i++) { acc = (acc + rd8(src + i)) & 0xFF; out[i] = (uint8_t)acc; }
    }
    put_out(dst, out, size, vram);
    free(out);
}

static double cos_t(u32 a) { return cos((double)(a & 0xFF) * 3.14159265358979323846 / 128.0); }
static double sin_t(u32 a) { return sin((double)(a & 0xFF) * 3.14159265358979323846 / 128.0); }

/* the maths in 1.14 fixed point, as the BIOS tables do */
static s32 fx(double v) { return (s32)floor(v * 16384.0 + 0.5); }

static void obj_affine_set(Cpu *c) {
    u32 src = c->r[0], dst = c->r[1], n = c->r[2], off = c->r[3];
    for (u32 i = 0; i < n; i++) {
        s32 sx = (int16_t)rd16(src), sy = (int16_t)rd16(src + 2);
        u32 th = rd16(src + 4) >> 8;
        src += 8;
        s32 cs = fx(cos_t(th)), sn = fx(sin_t(th));
        wr16(dst, (u32)((sx * cs) >> 14)); dst += off;
        wr16(dst, (u32)(-((sx * sn) >> 14))); dst += off;
        wr16(dst, (u32)((sy * sn) >> 14)); dst += off;
        wr16(dst, (u32)((sy * cs) >> 14)); dst += off;
    }
}

static void bg_affine_set(Cpu *c) {
    u32 src = c->r[0], dst = c->r[1], n = c->r[2];
    for (u32 i = 0; i < n; i++) {
        s32 ox = (s32)rd32(src), oy = (s32)rd32(src + 4);
        s32 cx = (int16_t)rd16(src + 8), cy = (int16_t)rd16(src + 10);
        s32 sx = (int16_t)rd16(src + 12), sy = (int16_t)rd16(src + 14);
        u32 th = rd16(src + 16) >> 8;
        src += 20;
        s32 cs = fx(cos_t(th)), sn = fx(sin_t(th));
        s32 pa = (sx * cs) >> 14, pb = -((sx * sn) >> 14), pc = (sy * sn) >> 14, pd = (sy * cs) >> 14;
        wr16(dst, (u32)pa); wr16(dst + 2, (u32)pb); wr16(dst + 4, (u32)pc); wr16(dst + 6, (u32)pd);
        wr32(dst + 8, (u32)(ox - (pa * cx + pb * cy)));
        wr32(dst + 12, (u32)(oy - (pc * cx + pd * cy)));
        dst += 16;
    }
}

static void intr_wait(Cpu *c, u32 discard, u32 mask) {
    io_set16(IO_IME, 1);
    if (discard) wr16(BIOS_IF, rd16(BIOS_IF) & ~mask);
    for (int guard = 0; guard < 4000; guard++) {
        if (rd16(BIOS_IF) & mask) { wr16(BIOS_IF, rd16(BIOS_IF) & ~mask); return; }
        sys_halt(c);
    }
    fprintf(stderr, "IntrWait(mask 0x%X) never completed\n", mask);
}

void bios_swi(Cpu *c, u32 n) {
    u32 *r = c->r;
    switch (n) {
    case 0x00: fprintf(stderr, "SoftReset\n"); exit(3);
    case 0x01: { /* RegisterRamReset */
        u32 f = r[0];
        if (f & 1) memset(rt_ewram, 0, sizeof rt_ewram);
        if (f & 2) memset(rt_iwram, 0, sizeof rt_iwram - 0x200);
        if (f & 4) memset(rt_pal, 0, sizeof rt_pal);
        if (f & 8) memset(rt_vram, 0, sizeof rt_vram);
        if (f & 16) memset(rt_oam, 0, sizeof rt_oam);
        if (f & 0x80) { u32 keep = io_get16(IO_KEYINPUT); memset(rt_io, 0, 0x100); io_set16(IO_KEYINPUT, keep); }
        break;
    }
    case 0x02: case 0x03: sys_halt(c); break; /* Halt, Stop */
    case 0x04: intr_wait(c, r[0], r[1]); break;
    case 0x05: intr_wait(c, 1, 1); break; /* VBlankIntrWait */
    case 0x06: { /* Div: r0 / r1 */
        s32 a = (s32)r[0], b = (s32)r[1];
        if (b == 0) { r[0] = a < 0 ? (u32)-1 : 1; r[1] = (u32)a; r[3] = 1; break; }
        s32 q = (a == (s32)0x80000000 && b == -1) ? (s32)0x80000000 : a / b;
        s32 m = (b == -1) ? 0 : a % b;
        r[0] = (u32)q; r[1] = (u32)m; r[3] = (u32)(q < 0 ? -q : q);
        break;
    }
    case 0x07: { u32 t = r[0]; r[0] = r[1]; r[1] = t; bios_swi(c, 6); break; } /* DivArm */
    case 0x08: { u32 x = r[0], v = 0; for (u32 bit = 1u << 15; bit; bit >>= 1) { u32 t = v | bit; if ((u64)t * t <= x) v = t; } r[0] = v; break; }
    case 0x09: { /* ArcTan (approximation of the BIOS routine's result: -0x4000..0x4000) */
        double t = atan((double)(s32)r[0] / 16384.0);
        r[0] = (u32)(s32)floor(t / 3.14159265358979323846 * 32768.0);
        break;
    }
    case 0x0A: { /* ArcTan2 */
        double t = atan2((double)(s32)r[1], (double)(s32)r[0]);
        if (t < 0) t += 2 * 3.14159265358979323846;
        r[0] = (u32)(t / (2 * 3.14159265358979323846) * 65536.0) & 0xFFFF;
        break;
    }
    case 0x0B: cpu_set(c, 0); break;
    case 0x0C: cpu_set(c, 1); break;
    case 0x0D: r[0] = 0xBAAE187F; break; /* GetBiosChecksum */
    case 0x0E: bg_affine_set(c); break;
    case 0x0F: obj_affine_set(c); break;
    case 0x11: lz77(c, 0); break;
    case 0x12: lz77(c, 1); break;
    case 0x14: rl(c, 0); break;
    case 0x15: rl(c, 1); break;
    case 0x16: diff_unfilter(c, 0, 0); break;
    case 0x17: diff_unfilter(c, 0, 1); break;
    case 0x18: diff_unfilter(c, 1, 1); break;
    case 0x19: break; /* SoundBias: the level changes gradually on the real BIOS; nothing to do without audio */
    default: fprintf(stderr, "BIOS call 0x%02X is not implemented (r0=%08X r1=%08X r2=%08X)\n", n, r[0], r[1], r[2]); break;
    }
}
