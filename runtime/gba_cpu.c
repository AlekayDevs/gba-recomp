/* The program status registers: CPSR flags and mode (with the banked r13/r14) and the SPSRs. */
#include "gba_rt.h"

u32 rt_mrs(Cpu *c) { return (c->n << 31) | (c->z << 30) | (c->c << 29) | (c->v << 28) | (c->cpsr_c ? c->cpsr_c : 0x1F); }

/* MSR CPSR_<fields>, v: f = flags (bit 3 of `fields`), c = control (bit 0): a mode switch banks r13 and r14 */
void rt_msr(Cpu *c, u32 fields, u32 v) {
    if (fields & 8) { c->n = v >> 31; c->z = (v >> 30) & 1; c->c = (v >> 29) & 1; c->v = (v >> 28) & 1; }
    if (fields & 1) {
        u32 old = (c->cpsr_c ? c->cpsr_c : 0x1F) & 0x1F, nw = v & 0x1F;
        if (old != nw) {
            c->bank_sp[old] = c->r[13]; c->bank_lr[old] = c->r[14];
            c->r[13] = c->bank_sp[nw]; c->r[14] = c->bank_lr[nw];
        }
        c->cpsr_c = v & 0xFF;
    }
}

/* the SPSR of the current mode (System and User have none: read as the CPSR) */
u32 rt_mrs_spsr(Cpu *c) {
    u32 m = (c->cpsr_c ? c->cpsr_c : 0x1F) & 0x1F;
    return m == 0x1F || m == 0x10 ? rt_mrs(c) : c->spsr[m];
}
void rt_msr_spsr(Cpu *c, u32 fields, u32 v) {
    u32 m = (c->cpsr_c ? c->cpsr_c : 0x1F) & 0x1F, old;
    if (m == 0x1F || m == 0x10) return;
    old = c->spsr[m];
    if (fields & 8) old = (old & 0x0FFFFFFFu) | (v & 0xF0000000u);
    if (fields & 1) old = (old & 0xFFFFFF00u) | (v & 0xFF);
    c->spsr[m] = old;
}
