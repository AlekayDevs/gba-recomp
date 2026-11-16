/* The bus of the console: BIOS (empty), EWRAM, IWRAM, I/O, palette, VRAM, OAM, ROM and the save area, with the mirrors and the
   odd rules of the hardware (unaligned reads rotate, byte writes to the palette and the BG part of VRAM repeat, to OAM they are ignored). */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>

uint8_t rt_ewram[0x40000], rt_iwram[0x8000], rt_pal[0x400], rt_vram[0x18000], rt_oam[0x400], rt_io[0x400];
uint8_t *rt_rom;
#ifdef RT_ROMSTAT
#ifndef ROM_SHIFT
#define ROM_SHIFT 12
#endif
/* which 4 KB pages of the ROM the CPU reads (to find fonts and tables while the game runs); counted only while rt_rom_stat_on */
u32 rt_rom_stat[1 << 20]; int rt_rom_stat_on;
/* a short history of the ROM addresses read (distinct 64-byte blocks, newest last) and, with rt_vtrace_lo/hi set, a line for every halfword written to that VRAM range */
u32 rt_vtrace_lo, rt_vtrace_hi, rt_vtrace_left; static u32 rom_ring[8]; static int rom_ring_n;
#define ROM_HIT(o) do { u32 oo_ = (o) & 0x01FFFFFF; if (rt_rom_stat_on) rt_rom_stat[oo_ >> ROM_SHIFT]++;     if (rt_vtrace_left && oo_ < 0xA00000 && (oo_ >> 6) != (rom_ring[(rom_ring_n + 7) & 7] >> 6)) rom_ring[rom_ring_n++ & 7] = oo_; } while (0)
u32 rt_wtrace_val, rt_wtrace_left;
static void wtrace(u32 a, u32 v, int bits) {
    if (!rt_wtrace_left) return;
    u32 want = rt_wtrace_val; int hit = 0;
    if (want == 0xFFFFFFFFu) { /* any write to the buffer area, printed when the ROM history changed since the last one */
        static u32 last_sig;
        if (a < 0x02036A00u || a >= 0x02037800u) return;
        u32 sig = 0; for (int i = 0; i < 8; i++) sig = sig * 31 + rom_ring[i];
        if (sig == last_sig) return;
        last_sig = sig; rt_wtrace_left--;
        printf("wwrite %08X = %08X (%d)  rom:", a, v, bits);
        for (int i = 0; i < 8; i++) printf(" %06X", rom_ring[(rom_ring_n + i) & 7]);
        printf("
");
        return;
    }
    if (bits == 16) hit = (v & 0xFFFF) == (want & 0xFFFF);
    else if (bits == 32) hit = (v & 0xFFFF) == (want & 0xFFFF) || (v >> 16) == (want & 0xFFFF);
    else hit = (v & 0xFF) == (want & 0xFF);
    if (!hit) return;
    rt_wtrace_left--;
    printf("wwrite %08X = %08X (%d)  rom:", a, v, bits);
    for (int i = 0; i < 8; i++) printf(" %06X", rom_ring[(rom_ring_n + i) & 7]);
    printf("
");
}
static void vtrace(u32 off, u32 v) {
    if (!rt_vtrace_left || off < rt_vtrace_lo || off >= rt_vtrace_hi || !v) return;
    rt_vtrace_left--;
    printf("vwrite %05X = %04X  rom:", off, v);
    for (int i = 0; i < 8; i++) printf(" %06X", rom_ring[(rom_ring_n + i) & 7]);
    printf("
");
}
#else
#define ROM_HIT(o) ((void)0)
#define wtrace(a, v, b) ((void)0)
#endif
u32 rt_rom_len;
static uint8_t sram[0x10000];

u32 io_get16(u32 off) { return (u32)rt_io[off] | ((u32)rt_io[off + 1] << 8); }
void io_set16(u32 off, u32 v) { rt_io[off] = (uint8_t)v; rt_io[off + 1] = (uint8_t)(v >> 8); }

static u32 vram_off(u32 a) {
    u32 o = a & 0x1FFFF;
    return o >= 0x18000 ? o - 0x8000 : o;
}

/* reads of 16 bits at an even offset of the I/O area */
static u32 io_read16(u32 off) {
    off &= 0x3FE;
    switch (off) {
    case IO_VCOUNT: sys_poll(); return io_get16(IO_VCOUNT);
    case IO_DISPSTAT: sys_poll(); return io_get16(IO_DISPSTAT);
    case IO_KEYINPUT: return ~sys_keys & 0x3FF;
    case 0x100: case 0x104: case 0x108: case 0x10C: { sys_poll(); return sys_timer_read((off - 0x100) / 4); } /* the counters run in sys */
    default: return io_get16(off);
    }
}

static void io_write16(u32 off, u32 v) {
    off &= 0x3FE;
    v &= 0xFFFF;
#ifdef RT_TRACE_IE
    if (off == IO_IE) rt_trace_ie(v);
#endif
    if (off >= 0xA0 && off < 0xA8) { apu_fifo_write(off, v, 2); return; }
    switch (off) {
    case IO_DISPSTAT: io_set16(off, (io_get16(off) & 7) | (v & ~7u)); return; /* bits 0-2 are status */
    case IO_VCOUNT: case IO_KEYINPUT: return;
    case IO_IF: io_set16(off, io_get16(off) & ~v); return; /* writing 1 acknowledges */
    case IO_IME: io_set16(off, v & 1); return;
    default: break;
    }
    if (off >= IO_DMA0 && off < IO_DMA0 + 48 && (off - IO_DMA0) % 12 == 10) { /* DMAxCNT_H */
        int ch = (off - IO_DMA0) / 12;
        u32 old = io_get16(off);
        io_set16(off, ch == 3 ? v : (v & 0xF7E0));
        if (!(old & 0x8000) && (v & 0x8000)) sys_dma_enable(ch);
        return;
    }
    if (off >= IO_TM0 && off < IO_TM0 + 16 && (off & 2)) { /* TMxCNT_H */
        u32 old = io_get16(off);
        io_set16(off, v);
        sys_timer_control((off - IO_TM0) / 4, old, v);
        return;
    }
    if (off >= IO_TM0 && off < IO_TM0 + 16) { /* TMxCNT_L: the reload value (the counter is a separate thing in sys) */
        io_set16(off, v);
        return;
    }
    io_set16(off, v);
    if (off >= 0x60 && off < 0xA0) apu_write16(off, v);
}

u32 rd8(u32 a) {
    switch (a >> 24) {
    case 0x02: return rt_ewram[a & 0x3FFFF];
    case 0x03: return rt_iwram[a & 0x7FFF];
    case 0x04: { if ((a & 0xFFFFFF) >= 0x400) return 0; u32 v = io_read16(a & ~1u); return (a & 1) ? v >> 8 : v & 0xFF; }
    case 0x05: return rt_pal[a & 0x3FF];
    case 0x06: return rt_vram[vram_off(a)];
    case 0x07: return rt_oam[a & 0x3FF];
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        u32 o = a & 0x01FFFFFF;
        if ((a >> 24) == 0x0D && 0) return 1;
        ROM_HIT(o);
        return o < rt_rom_len ? rt_rom[o] : (a >> 1) & 0xFF;
    }
    case 0x0E: case 0x0F: return sram[a & 0xFFFF];
    default: return 0;
    }
}

u32 rd16(u32 a) {
    u32 v;
    switch (a >> 24) {
    case 0x04: v = (a & 0xFFFFFF) < 0x400 ? io_read16(a & ~1u) : 0; break;
    case 0x0D:
        /* the EEPROM (at 0x0D000000 on 16 MB ROMs, at its top on bigger ones): one bit per halfword read */
        if (rt_rom_len <= 0x1000000 || (a & 0x01FFFFFF) >= 0x01FFFF00) { v = eeprom_read_bit(); break; }
        v = rd8(a & ~1u) | (rd8((a & ~1u) + 1) << 8);
        break;
    default: v = rd8(a & ~1u) | (rd8((a & ~1u) + 1) << 8); break;
    }
    return (a & 1) ? (v >> 8) | (v << 24) : v;
}
u32 rdsh(u32 a) { return (a & 1) ? (u32)(s32)(int8_t)rd8(a) : (u32)(s32)(int16_t)rd16(a); }
u32 rdsb(u32 a) { return (u32)(s32)(int8_t)rd8(a); }

u32 rd32(u32 a) {
    u32 b = a & ~3u, v;
    switch (a >> 24) {
    case 0x02: v = *(u32 *)(rt_ewram + (b & 0x3FFFF)); break;
    case 0x03: v = *(u32 *)(rt_iwram + (b & 0x7FFF)); break;
    case 0x04: v = (a & 0xFFFFFF) < 0x400 ? io_read16(b) | (io_read16(b + 2) << 16) : 0; break;
    case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: {
        u32 o = b & 0x01FFFFFF;
        ROM_HIT(o);
        if (o + 4 <= rt_rom_len) { v = *(u32 *)(rt_rom + o); break; }
    } /* fall through */
    default: v = rd8(b) | (rd8(b + 1) << 8) | (rd8(b + 2) << 16) | ((u32)rd8(b + 3) << 24); break;
    }
    u32 s = (a & 3) * 8;
    return s ? (v >> s) | (v << (32 - s)) : v;
}

void wr8(u32 a, u32 v) {
    v &= 0xFF;
    switch (a >> 24) {
    case 0x02: wtrace(a, v, 8); rt_ewram[a & 0x3FFFF] = (uint8_t)v; break;
    case 0x03: wtrace(a, v, 8); rt_iwram[a & 0x7FFF] = (uint8_t)v; break;
    case 0x04: {
        if ((a & 0xFFFFFF) >= 0x400) break;
        if ((a & 0xFFFFFF) >= 0xA0 && (a & 0xFFFFFF) < 0xA8) { apu_fifo_write(a & 0xFFFFFF, v, 1); break; }
        u32 h = a & ~1u, cur = io_get16(h & 0x3FE);
        /* the 16-bit register with the byte replaced; IF and the like get the merged value, which is what a byte write means for them */
        if (h == (IO_IF)) { io_write16(h, (a & 1) ? v << 8 : v); break; }
        io_write16(h, (a & 1) ? (cur & 0xFF) | (v << 8) : (cur & 0xFF00) | v);
        break;
    }
    case 0x05: rt_pal[a & 0x3FE] = (uint8_t)v; rt_pal[(a & 0x3FE) + 1] = (uint8_t)v; break;
    case 0x06: {
        u32 o = vram_off(a);
        u32 dispcnt = io_get16(IO_DISPCNT), mode = dispcnt & 7;
        u32 limit = mode >= 3 ? 0x14000 : 0x10000;
        if (o < limit) { rt_vram[o & ~1u] = (uint8_t)v; rt_vram[(o & ~1u) + 1] = (uint8_t)v; }
        break;
    }
    case 0x07: break;
    case 0x0E: case 0x0F: sram[a & 0xFFFF] = (uint8_t)v; break;
    default: break;
    }
}

void wr16(u32 a, u32 v) {
    a &= ~1u;
    switch (a >> 24) {
    case 0x02: wtrace(a, v, 16); *(uint16_t *)(rt_ewram + (a & 0x3FFFE)) = (uint16_t)v; break;
    case 0x03: wtrace(a, v, 16); *(uint16_t *)(rt_iwram + (a & 0x7FFE)) = (uint16_t)v; break;
    case 0x04: if ((a & 0xFFFFFF) < 0x400) io_write16(a, v); break;
    case 0x05: *(uint16_t *)(rt_pal + (a & 0x3FE)) = (uint16_t)v; break;
    case 0x06:
#ifdef RT_ROMSTAT
        vtrace(vram_off(a) & ~1u, v & 0xFFFF);
#endif
        *(uint16_t *)(rt_vram + (vram_off(a) & ~1u)) = (uint16_t)v; break;
    case 0x07: *(uint16_t *)(rt_oam + (a & 0x3FE)) = (uint16_t)v; break;
    case 0x0E: case 0x0F: sram[a & 0xFFFF] = (uint8_t)v; break;
    default: break;
    }
}

void wr32(u32 a, u32 v) {
    a &= ~3u;
    switch (a >> 24) {
    case 0x02: wtrace(a, v, 32); *(u32 *)(rt_ewram + (a & 0x3FFFC)) = v; break;
    case 0x03: wtrace(a, v, 32); *(u32 *)(rt_iwram + (a & 0x7FFC)) = v; break;
    case 0x04: if ((a & 0xFFFFFF) < 0x400) { io_write16(a, v & 0xFFFF); io_write16(a + 2, v >> 16); } break;
    default: wr16(a, v & 0xFFFF); wr16(a + 2, v >> 16); break;
    }
}


void rt_untranslated(Cpu *c, u32 addr) { (void)c; fprintf(stderr, "untranslated function 0x%08X\n", addr); }
u32 rt_site;
void rt_missing(Cpu *c, u32 target) { (void)c; fprintf(stderr, "jump to 0x%08X from 0x%08X, not a translated function\n", target, rt_site); fflush(stderr); exit(4); }
void rt_swi(Cpu *c, u32 n) { bios_swi(c, n); }
