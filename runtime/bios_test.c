/* Checks of the BIOS routines against values worked out from the documented behaviour.
   tcc -Iruntime -o bios_test.exe runtime/bios_test.c runtime/gba_bios.c runtime/gba_bus.c runtime/gba_sys.c runtime/gba_ppu.c runtime/gba_apu.c runtime/gba_state.c runtime/gba_save.c runtime/gba_cpu.c && ./bios_test.exe */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void sys_report(void) {}
void rt_dispatch(Cpu *c, u32 t) { (void)c; (void)t; }

static int fails, total;
#define CHECK(cond, ...) do { total++; if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define EQ(a, b) CHECK((u32)(a) == (u32)(b), #a " = %08X, expected %08X", (u32)(a), (u32)(b))
#define E 0x02000000u

static Cpu c;
static void call(u32 n, u32 r0, u32 r1, u32 r2, u32 r3) { c.r[0] = r0; c.r[1] = r1; c.r[2] = r2; c.r[3] = r3; bios_swi(&c, n); }

int main(void) {
    /* Div */
    call(6, 7, 2, 0, 0); EQ(c.r[0], 3); EQ(c.r[1], 1); EQ(c.r[3], 3);
    call(6, (u32)-7, 2, 0, 0); EQ(c.r[0], (u32)-3); EQ(c.r[1], (u32)-1); EQ(c.r[3], 3);
    call(6, 7, (u32)-2, 0, 0); EQ(c.r[0], (u32)-3); EQ(c.r[1], 1);
    call(7, 2, 7, 0, 0); EQ(c.r[0], 3); EQ(c.r[1], 1); /* DivArm: swapped operands */
    /* Sqrt */
    call(8, 0x10000, 0, 0, 0); EQ(c.r[0], 0x100);
    call(8, 99, 0, 0, 0); EQ(c.r[0], 9);
    call(8, 0xFFFFFFFF, 0, 0, 0); EQ(c.r[0], 0xFFFF);
    /* ArcTan2(x, y): the angle as a fraction of 0x10000 */
    call(0x0A, 1, 0, 0, 0); EQ(c.r[0], 0);
    call(0x0A, 0, 1, 0, 0); EQ(c.r[0], 0x4000);
    call(0x0A, (u32)-1, 0, 0, 0); EQ(c.r[0], 0x8000);
    call(0x0A, 0, (u32)-1, 0, 0); EQ(c.r[0], 0xC000);
    call(0x0A, 1, 1, 0, 0); EQ(c.r[0], 0x2000);
    /* ObjAffineSet: scale 1.0 (8.8) and 0, 90 and 180 degrees; the output halfwords are `offset` bytes apart */
    wr16(E + 0, 0x100); wr16(E + 2, 0x100); wr16(E + 4, 0x0000);
    wr16(E + 8, 0x200); wr16(E + 10, 0x100); wr16(E + 12, 0x4000);
    wr16(E + 16, 0x100); wr16(E + 18, 0x100); wr16(E + 20, 0x8000);
    call(0x0F, E, E + 0x100, 3, 2);
    EQ(rd16(E + 0x100), 0x100); EQ(rd16(E + 0x102), 0); EQ(rd16(E + 0x104), 0); EQ(rd16(E + 0x106), 0x100);
    EQ(rd16(E + 0x108), 0); EQ(rd16(E + 0x10A), 0xFE00); EQ(rd16(E + 0x10C), 0x100); EQ(rd16(E + 0x10E), 0); /* sx = 2.0: pb = -sx*sin(90) */
    EQ(rd16(E + 0x110), 0xFF00); EQ(rd16(E + 0x112), 0); EQ(rd16(E + 0x114), 0); EQ(rd16(E + 0x116), 0xFF00);
    /* ObjAffineSet to a stride of 8 (an OAM entry): the four parameters of consecutive entries */
    call(0x0F, E, E + 0x200, 1, 8);
    EQ(rd16(E + 0x200), 0x100); EQ(rd16(E + 0x208), 0); EQ(rd16(E + 0x210), 0); EQ(rd16(E + 0x218), 0x100);
    /* BgAffineSet: identity around the centre: the start point is the origin */
    wr32(E + 0x300, 120 << 8); wr32(E + 0x304, 80 << 8); wr16(E + 0x308, 120); wr16(E + 0x30A, 80);
    wr16(E + 0x30C, 0x100); wr16(E + 0x30E, 0x100); wr16(E + 0x310, 0);
    call(0x0E, E + 0x300, E + 0x400, 1, 0);
    EQ(rd16(E + 0x400), 0x100); EQ(rd16(E + 0x402), 0); EQ(rd16(E + 0x404), 0); EQ(rd16(E + 0x406), 0x100);
    EQ(rd32(E + 0x408), 0); EQ(rd32(E + 0x40C), 0);
    /* BgAffineSet at 90 degrees: pa = 0, pb = -1, pc = 1, pd = 0 (8.8); start = origin - (pa*cx + pb*cy, pc*cx + pd*cy) */
    wr32(E + 0x300, 0); wr32(E + 0x304, 0); wr16(E + 0x308, 10); wr16(E + 0x30A, 20);
    wr16(E + 0x30C, 0x100); wr16(E + 0x30E, 0x100); wr16(E + 0x310, 0x4000);
    call(0x0E, E + 0x300, E + 0x400, 1, 0);
    EQ(rd16(E + 0x400), 0); EQ(rd16(E + 0x402), 0xFF00); EQ(rd16(E + 0x404), 0x100); EQ(rd16(E + 0x406), 0);
    EQ(rd32(E + 0x408), 20 * 0x100); EQ(rd32(E + 0x40C), (u32)(-10 * 0x100));
    /* CpuSet: copy halfwords, fill words; CpuFastSet rounds the count up to 8 words */
    for (int i = 0; i < 8; i++) wr16(E + 0x500 + i * 2, 0x1100 + i);
    call(0x0B, E + 0x500, E + 0x520, 8, 0);
    EQ(rd16(E + 0x520), 0x1100); EQ(rd16(E + 0x52E), 0x1107);
    wr32(E + 0x540, 0xDEADBEEF);
    call(0x0B, E + 0x540, E + 0x560, 4 | (1 << 24) | (1 << 26), 0);
    EQ(rd32(E + 0x560), 0xDEADBEEF); EQ(rd32(E + 0x56C), 0xDEADBEEF); EQ(rd32(E + 0x570), 0);
    call(0x0C, E + 0x540, E + 0x580, 3 | (1 << 24), 0);
    EQ(rd32(E + 0x580), 0xDEADBEEF); EQ(rd32(E + 0x59C), 0xDEADBEEF); /* 3 words -> 8 */
    /* LZ77: 4 literals "ABCD" then a copy of 4 bytes from 4 back (disp 3, len 4 -> b1 = 0x10 | 0, b2 = 3) */
    {
        static const uint8_t z[] = {0x10, 0x08, 0, 0, 0x08, 'A', 'B', 'C', 'D', 0x10, 0x03};
        for (u32 i = 0; i < sizeof z; i++) wr8(E + 0x600 + i, z[i]);
        call(0x11, E + 0x600, E + 0x700, 0, 0);
        EQ(rd32(E + 0x700), 0x44434241); EQ(rd32(E + 0x704), 0x44434241);
    }
    /* RLUnComp: a run of 5 'X' (flag 0x82), then 3 literals (flag 0x02) */
    {
        static const uint8_t z[] = {0x30, 0x08, 0, 0, 0x82, 'X', 0x02, 'a', 'b', 'c'};
        for (u32 i = 0; i < sizeof z; i++) wr8(E + 0x800 + i, z[i]);
        call(0x14, E + 0x800, E + 0x900, 0, 0);
        EQ(rd32(E + 0x900), 0x58585858); EQ(rd32(E + 0x904), 0x63626158);
    }
    /* BitUnPack: 1 bit -> 4 bits with offset 1 (0 -> 0 stays 0 without the zero flag): 0b00000101 -> the set bits become 2 (1 + offset), nibbles 2,0,2,0,... */
    {
        wr8(E + 0xA00, 0x05);
        wr16(E + 0xA10, 1); wr8(E + 0xA12, 1); wr8(E + 0xA13, 4); wr32(E + 0xA14, 1);
        call(0x10, E + 0xA00, E + 0xB00, E + 0xA10, 0);
        EQ(rd32(E + 0xB00), 0x00000202);
    }
    /* HuffUnComp, 8-bit symbols: root with two data leaves 'a' (bit 0) and 'b' (bit 1); bits 0,1,1,0 */
    {
        static const uint8_t t[] = {0x28, 0x04, 0, 0, 0x01, 0xC0, 'a', 'b'};
        for (u32 i = 0; i < sizeof t; i++) wr8(E + 0xC00 + i, t[i]);
        wr32(E + 0xC08, 0x60000000u);
        call(0x13, E + 0xC00, E + 0xD00, 0, 0);
        EQ(rd32(E + 0xD00), 0x61626261u);
    }
    printf("%d checks, %d failed\n", total, fails);
    return fails != 0;
}
