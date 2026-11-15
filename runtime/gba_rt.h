/* Runtime of the generated C (spike): CPU state, flags, shifts and the bus. */
#ifndef GBA_RT_H
#define GBA_RT_H
#include <stdint.h>
typedef uint32_t u32;
typedef int32_t s32;
typedef uint64_t u64;

typedef struct {
    u32 r[16];
    u32 n, z, c, v;
    u32 cpsr_c; /* mode and the I/F/T bits */
    u32 spsr[32]; /* SPSR of every exception mode (indexed by the mode bits) */
    u32 bank_sp[32], bank_lr[32]; /* r13/r14 of every mode, indexed by the mode bits (the current mode's live in r[]) */
} Cpu;

/* bus: byte, halfword and word accesses; the unaligned rules of the ARM7TDMI live in the implementation */
u32 rd32(u32 a);
u32 rd16(u32 a);
u32 rd8(u32 a);
u32 rdsh(u32 a);
u32 rdsb(u32 a);
u32 rt_mrs(Cpu *c);
void rt_msr(Cpu *c, u32 fields, u32 v);
u32 rt_mrs_spsr(Cpu *c);
void rt_msr_spsr(Cpu *c, u32 fields, u32 v);
/* time: a backward branch is a place where time can pass and interrupts can be taken */
extern int rt_budget;
void rt_yield(Cpu *c);
/* save states: the generated code of the profile's [resume] address calls rt_state_point when a request is pending */
extern volatile int rt_state_req;
void rt_state_point(Cpu *c);
/* a backward branch at `a`: when it has run long enough, time passes (and the site is noted for diagnostics) */
/* optional call trace (compile with -DRT_TRACE and provide rt_enter): called at the start of every translated function */
/* non-local exit: a callee that leaves r13 at or above the one this function started with has discarded this
   frame (the game's entity loop restores a saved sp, like longjmp), so this function returns too */
#define RT_UNWIND() do { if (r13 >= sp0_) { STORE(); return; } } while (0)
#ifdef RT_TRACE
void rt_enter(Cpu *c, u32 addr);
#define RT_ENTER(a) rt_enter(c, (a))
/* the call contract: after a call returns, r13 is what it was before the call */
void rt_sp_mismatch(Cpu *c, u32 site, u32 target, u32 before, u32 after);
#define RT_DECL() u32 sp_chk_, tg_chk_, sp0_ = r13; (void)sp0_
#define RT_TG(x) (tg_chk_ = (x))
#define RT_CHK_BEFORE() (sp_chk_ = r13)
#define RT_CHK_AFTER(s, t) do { if (r13 != sp_chk_) { rt_sp_mismatch(c, (s), (t), sp_chk_, r13); RT_UNWIND(); } } while (0)
#else
#define RT_ENTER(a) ((void)0)
#define RT_DECL() u32 sp_chk_, sp0_ = r13; (void)sp0_; (void)sp_chk_
#define RT_TG(x) ((void)0)
#define RT_TG(x) ((void)0)
#define RT_CHK_BEFORE() (sp_chk_ = r13)
#define RT_CHK_AFTER(s, t) do { if (r13 != sp_chk_) RT_UNWIND(); } while (0)
#endif
#define YIELD_AT(a) do { if (--rt_budget < 0) { rt_site = (a); STORE(); rt_yield(c); RELOAD(); } } while (0)
#define YIELD() do { if (--rt_budget < 0) { STORE(); rt_yield(c); RELOAD(); } } while (0)
void wr32(u32 a, u32 v);
void wr16(u32 a, u32 v);
void wr8(u32 a, u32 v);
void rt_untranslated(Cpu *c, u32 addr);
/* BIOS call (SWI n) */
void rt_swi(Cpu *c, u32 n);
/* jump to the code at a run-time address (a tail call): generated, looks the address up among the translated functions */
extern u32 rt_site; /* the address of the instruction that made the last run-time jump (for diagnostics) */
void rt_dispatch(Cpu *c, u32 target);
/* what rt_dispatch does when the address is not a translated function */
void rt_missing(Cpu *c, u32 target);

#define LOAD() \
    u32 r0 = c->r[0], r1 = c->r[1], r2 = c->r[2], r3 = c->r[3], r4 = c->r[4], r5 = c->r[5], r6 = c->r[6], r7 = c->r[7], \
        r8 = c->r[8], r9 = c->r[9], r10 = c->r[10], r11 = c->r[11], r12 = c->r[12], r13 = c->r[13], r14 = c->r[14]; \
    u32 N = c->n, Z = c->z, C = c->c, V = c->v
/* after a call the registers are the callee's */
#define RELOAD() \
    do { r0 = c->r[0]; r1 = c->r[1]; r2 = c->r[2]; r3 = c->r[3]; r4 = c->r[4]; r5 = c->r[5]; r6 = c->r[6]; r7 = c->r[7]; \
         r8 = c->r[8]; r9 = c->r[9]; r10 = c->r[10]; r11 = c->r[11]; r12 = c->r[12]; r13 = c->r[13]; r14 = c->r[14]; \
         N = c->n; Z = c->z; C = c->c; V = c->v; } while (0)
#define STORE() \
    do { c->r[0] = r0; c->r[1] = r1; c->r[2] = r2; c->r[3] = r3; c->r[4] = r4; c->r[5] = r5; c->r[6] = r6; c->r[7] = r7; \
         c->r[8] = r8; c->r[9] = r9; c->r[10] = r10; c->r[11] = r11; c->r[12] = r12; c->r[13] = r13; c->r[14] = r14; \
         c->n = N; c->z = Z; c->c = C; c->v = V; } while (0)

#define SETNZ(x) do { u32 x_ = (x); N = x_ >> 31; Z = x_ == 0; } while (0)
#define ADDS(d, a, b) do { u32 a_ = (a), b_ = (b), r_ = a_ + b_; C = r_ < a_; V = ((a_ ^ r_) & (b_ ^ r_)) >> 31; (d) = r_; SETNZ(r_); } while (0)
#define SUBS(d, a, b) do { u32 a_ = (a), b_ = (b), r_ = a_ - b_; C = a_ >= b_; V = ((a_ ^ b_) & (a_ ^ r_)) >> 31; (d) = r_; SETNZ(r_); } while (0)
#define CMP(a, b) do { u32 a_ = (a), b_ = (b), r_ = a_ - b_; C = a_ >= b_; V = ((a_ ^ b_) & (a_ ^ r_)) >> 31; SETNZ(r_); } while (0)
#define CMN(a, b) do { u32 a_ = (a), b_ = (b), r_ = a_ + b_; C = r_ < a_; V = ((a_ ^ r_) & (b_ ^ r_)) >> 31; SETNZ(r_); } while (0)
#define ADCS(d, a, b) do { u32 a_ = (a), b_ = (b); u64 t_ = (u64)a_ + b_ + C; u32 r_ = (u32)t_; C = (u32)(t_ >> 32); V = ((a_ ^ r_) & (b_ ^ r_)) >> 31; (d) = r_; SETNZ(r_); } while (0)
#define SBCS(d, a, b) do { u32 a_ = (a), b_ = (b); u64 t_ = (u64)a_ + (u32)~b_ + C; u32 r_ = (u32)t_; C = (u32)(t_ >> 32); V = ((a_ ^ b_) & (a_ ^ r_)) >> 31; (d) = r_; SETNZ(r_); } while (0)

static inline u32 lsl_c(u32 a, u32 n, u32 *c) {
    if (n == 0) return a;
    if (n < 32) { *c = (a >> (32 - n)) & 1; return a << n; }
    *c = n == 32 ? a & 1 : 0;
    return 0;
}
static inline u32 lsr_c(u32 a, u32 n, u32 *c) {
    if (n == 0) return a;
    if (n < 32) { *c = (a >> (n - 1)) & 1; return a >> n; }
    *c = n == 32 ? a >> 31 : 0;
    return 0;
}
static inline u32 asr_c(u32 a, u32 n, u32 *c) {
    if (n == 0) return a;
    if (n < 32) { *c = ((s32)a >> (n - 1)) & 1; return (u32)((s32)a >> n); }
    *c = a >> 31;
    return (u32)((s32)a >> 31);
}
static inline u32 ror_c(u32 a, u32 n, u32 *c) {
    if (n == 0) return a;
    n &= 31;
    if (n == 0) { *c = a >> 31; return a; }
    *c = (a >> (n - 1)) & 1;
    return (a >> n) | (a << (32 - n));
}
#endif
