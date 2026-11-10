/* Flat memory for the spike: ROM, EWRAM and IWRAM as arrays; any other address sets rt_fault, which the test treats as "skip this case". */
#include "gba_rt.h"

uint8_t *rt_rom;
u32 rt_rom_len;
uint8_t rt_ewram[0x40000], rt_iwram[0x8000];
int rt_fault;
u32 rt_fault_addr; /* the first address that faulted (for the test's report) */
int rt_unaligned; /* a halfword/word access that was not aligned: the ARM7TDMI rotates, newer cores do not, so the reference test skips it */

static uint8_t *at(u32 a, u32 n) {
    if (a >= 0x08000000u && a < 0x0E000000u) {
        u32 o = a & 0x01FFFFFFu;
        if (o + n <= rt_rom_len) return rt_rom + o;
    } else if (a >= 0x02000000u && a < 0x02040000u) {
        return rt_ewram + (a - 0x02000000u);
    } else if (a >= 0x03000000u && a < 0x03008000u) {
        return rt_iwram + (a - 0x03000000u);
    }
    if (!rt_fault) rt_fault_addr = a;
    rt_fault = 1;
    return 0;
}
static int writable(u32 a) { return (a >> 24) == 2 || (a >> 24) == 3; }

u32 rd8(u32 a) { uint8_t *p = at(a, 1); return p ? *p : 0; }
u32 rd16(u32 a) {
    if (a & 1) rt_unaligned = 1;
    uint8_t *p = at(a & ~1u, 2);
    u32 v = p ? (u32)p[0] | ((u32)p[1] << 8) : 0;
    return (a & 1) ? (v >> 8) | (v << 24) : v;
}
u32 rdsh(u32 a) {
    if (a & 1) rt_unaligned = 1;
    if (a & 1) return (u32)(s32)(int8_t)rd8(a);
    return (u32)(s32)(int16_t)rd16(a);
}
u32 rd32(u32 a) {
    if (a & 3) rt_unaligned = 1;
    uint8_t *p = at(a & ~3u, 4);
    u32 v = p ? (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24) : 0;
    u32 s = (a & 3) * 8;
    return s ? (v >> s) | (v << (32 - s)) : v;
}
void wr8(u32 a, u32 v) {
    uint8_t *p = at(a, 1);
    if (p && writable(a)) *p = (uint8_t)v; else rt_fault = 1;
}
void wr16(u32 a, u32 v) {
    if (a & 1) rt_unaligned = 1;
    uint8_t *p = at(a & ~1u, 2);
    if (p && writable(a)) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); } else rt_fault = 1;
}
void wr32(u32 a, u32 v) {
    if (a & 3) rt_unaligned = 1;
    uint8_t *p = at(a & ~3u, 4);
    if (p && writable(a)) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
    else rt_fault = 1;
}

u32 rt_untranslated_addr;
void rt_untranslated(Cpu *c, u32 addr) { (void)c; rt_untranslated_addr = addr; rt_fault = 1; }
void rt_swi(Cpu *c, u32 n) { (void)c; rt_untranslated_addr = 0xB1050000u | n; rt_fault = 1; }
void rt_missing(Cpu *c, u32 target) { (void)c; rt_untranslated_addr = target | 1; rt_fault = 1; }
u32 rdsb(u32 a) { return (u32)(s32)(int8_t)rd8(a); }
u32 rt_site;
int rt_budget = 0x7FFFFFFF;
void rt_yield(Cpu *c) { (void)c; rt_budget = 0x7FFFFFFF; }
