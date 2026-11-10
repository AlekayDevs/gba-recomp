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
} Cpu;

/* bus: byte, halfword and word accesses; the unaligned rules of the ARM7TDMI live in the implementation */
u32 rd32(u32 a);
u32 rd16(u32 a);
u32 rd8(u32 a);
u32 rdsh(u32 a);
void wr32(u32 a, u32 v);
void wr16(u32 a, u32 v);
void wr8(u32 a, u32 v);
void rt_untranslated(Cpu *c, u32 addr);

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
