/* What happens around the CPU: frames and lines, interrupts, DMA and timers.
   The generated code runs as one thread; waiting for an interrupt (Halt, VBlankIntrWait) advances time inside the call. */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

u32 sys_frames, sys_max_frames, sys_keys;
static int in_irq;
static Cpu *cpu_ptr;
static u32 dma_src[4], dma_dst[4], dma_cnt[4];
static u32 timer_cnt[4], timer_acc[4];

void ppu_line(int line);
void ppu_frame_done(void);
void (*sys_frame_hook)(u32 frame); /* called when a frame ends (the headless runner presses keys and takes screenshots here) */

void sys_init(Cpu *c) {
    cpu_ptr = c;
    memset(c, 0, sizeof *c);
    c->cpsr_c = 0x1F; /* System mode, as the BIOS leaves it when it jumps to the ROM */
    c->r[13] = 0x03007F00;
    c->bank_sp[0x12] = 0x03007FA0; /* IRQ */
    c->bank_sp[0x13] = 0x03007FE0; /* SVC */
    io_set16(IO_DISPCNT, 0x0080);  /* forced blank */
    io_set16(IO_KEYINPUT, 0x3FF);
}

void sys_raise(u32 mask) { io_set16(IO_IF, io_get16(IO_IF) | mask); }

void sys_run_irq(Cpu *c) {
    if (in_irq || !(io_get16(IO_IME) & 1) || !(io_get16(IO_IE) & io_get16(IO_IF))) return;
    u32 handler = rd32(0x03007FFC);
    if (!handler) return;
    in_irq = 1;
    Cpu saved = *c;
    c->spsr[0x12] = rt_mrs(c);
    rt_msr(c, 1, 0x92); /* IRQ mode: banks sp and lr */
    c->r[0] = 0x04000000;
    c->r[14] = 0;
    rt_dispatch(c, handler);
    *c = saved;
    in_irq = 0;
}

/* ---- DMA ---- */
static void dma_run(int ch) {
    u32 base = IO_DMA0 + ch * 12;
    u32 ctl = io_get16(base + 10);
    u32 n = dma_cnt[ch];
    int w32 = (ctl >> 10) & 1;
    int dstc = (ctl >> 5) & 3, srcc = (ctl >> 7) & 3;
    u32 step = w32 ? 4 : 2;
    u32 src = dma_src[ch], dst = dma_dst[ch];
    if (!w32 && (dst >> 24) == 0x0D) {
        /* to the EEPROM: one bit per halfword */
        uint8_t bits[256];
        u32 m = n > 256 ? 256 : n;
        for (u32 i = 0; i < m; i++) { bits[i] = rd16(src) & 1; src += srcc == 1 ? -step : (srcc == 2 ? 0 : step); }
        eeprom_write_bits(bits, m);
        n = 0;
    } else if (!w32 && (src >> 24) == 0x0D) {
        for (u32 i = 0; i < n; i++) {
            wr16(dst, eeprom_read_bit());
            dst += dstc == 1 ? -step : (dstc == 2 ? 0 : step);
        }
        n = 0;
    }
    for (u32 i = 0; i < n; i++) {
        if (w32) wr32(dst, rd32(src)); else wr16(dst, rd16(src));
        dst += dstc == 1 ? -step : (dstc == 2 ? 0 : step);
        src += srcc == 1 ? -step : (srcc == 2 ? 0 : step);
    }
    dma_src[ch] = src;
    dma_dst[ch] = dstc == 3 ? dma_dst[ch] : dst;
    if (ctl & 0x4000) sys_raise(1u << (8 + ch));
    if ((ctl & 0x200) && ((ctl >> 12) & 3) != 0) {
        /* repeat: reload the count (and the destination when it was "increment + reload") */
        u32 cl = io_get16(base + 8);
        dma_cnt[ch] = cl ? cl : (ch == 3 ? 0x10000 : 0x4000);
        if (dstc == 3) dma_dst[ch] = io_get16(base + 4) | (io_get16(base + 6) << 16);
    } else {
        io_set16(base + 10, ctl & ~0x8000u);
    }
}

void sys_dma_enable(int ch) {
    u32 base = IO_DMA0 + ch * 12;
    u32 ctl = io_get16(base + 10);
    u32 cl = io_get16(base + 8);
    dma_src[ch] = (io_get16(base) | (io_get16(base + 2) << 16)) & (ch == 0 ? 0x07FFFFFF : 0x0FFFFFFF);
    dma_dst[ch] = (io_get16(base + 4) | (io_get16(base + 6) << 16)) & (ch == 3 ? 0x0FFFFFFF : 0x07FFFFFF);
    dma_cnt[ch] = cl ? cl : (ch == 3 ? 0x10000 : 0x4000);
    if (((ctl >> 12) & 3) == 0) dma_run(ch);
}

/* run the channels that wait for this timing (1 = VBlank, 2 = HBlank) */
static void dma_timing(int t) {
    for (int ch = 0; ch < 4; ch++) {
        u32 ctl = io_get16(IO_DMA0 + ch * 12 + 10);
        if ((ctl & 0x8000) && (int)((ctl >> 12) & 3) == t) dma_run(ch);
    }
}

/* ---- timers ---- */
void sys_timer_control(int t, u32 old, u32 nw) {
    if (!(old & 0x80) && (nw & 0x80)) { timer_cnt[t] = io_get16(IO_TM0 + t * 4); timer_acc[t] = 0; }
}
u32 sys_timer_read(int t) { return timer_cnt[t]; }

static void timers_advance(u32 cycles) {
    static const u32 pre[4] = {1, 64, 256, 1024};
    u32 carry = 0; /* overflows of the previous timer, for the cascade */
    for (int t = 0; t < 4; t++) {
        u32 ctl = io_get16(IO_TM0 + t * 4 + 2);
        u32 over = 0;
        if (!(ctl & 0x80)) { carry = 0; continue; }
        u32 ticks;
        if ((ctl & 4) && t > 0) ticks = carry;
        else { timer_acc[t] += cycles; ticks = timer_acc[t] / pre[ctl & 3]; timer_acc[t] %= pre[ctl & 3]; }
        u32 reload = io_get16(IO_TM0 + t * 4);
        u32 v = timer_cnt[t] + ticks;
        if (v > 0xFFFF) {
            u32 period = 0x10000 - reload;
            over = 1 + (v - 0x10000) / (period ? period : 1);
            v = reload + (v - 0x10000) % (period ? period : 1);
        }
        timer_cnt[t] = v;
        if (over && (ctl & 0x40)) sys_raise(1u << (3 + t));
        carry = over;
    }
}

/* ---- time: one scanline at a time ---- */
static int cur_line;
static u32 poll_cycles;
extern void sys_report(void);

static void sys_step_line(Cpu *c, int deliver) {
    int line = cur_line;
    u32 ds = io_get16(IO_DISPSTAT);
    io_set16(IO_VCOUNT, line);
    ds &= ~7u;
    if (line >= 160 && line != 227) ds |= 1;
    if (line == (int)(ds >> 8)) { ds |= 4; if (ds & 0x20) sys_raise(4); }
    io_set16(IO_DISPSTAT, ds);
    if (line < 160) ppu_line(line);
    if (line == 160) {
        if (ds & 8) sys_raise(1);
        dma_timing(1);
    }
    if (line < 160) {
        io_set16(IO_DISPSTAT, io_get16(IO_DISPSTAT) | 2); /* HBlank */
        if (ds & 0x10) sys_raise(2);
        dma_timing(2);
    }
    timers_advance(1232);
    if (deliver) sys_run_irq(c);
    io_set16(IO_DISPSTAT, io_get16(IO_DISPSTAT) & ~2u);
    if (++cur_line == 228) {
        cur_line = 0;
        ppu_frame_done();
        sys_frames++;
        if (sys_frame_hook) sys_frame_hook(sys_frames);
        if (sys_max_frames && sys_frames >= sys_max_frames) {
            fprintf(stderr, "stopped after %u frames
", sys_frames);
            sys_report();
            exit(0);
        }
    }
}

/* one whole frame from wherever we are */
void sys_frame(Cpu *c) {
    int start = sys_frames;
    while ((int)sys_frames == start) sys_step_line(c, 1);
}

/* Halt: the CPU sleeps until an interrupt that is enabled is raised */
void sys_halt(Cpu *c) {
    for (int i = 0; i < 228 * 2; i++) {
        sys_step_line(c, 1);
        if (io_get16(IO_IE) & io_get16(IO_IF)) return;
    }
}

/* a read of a register that changes with time (VCOUNT, DISPSTAT, timers, IF) is a poll: each costs some cycles, and time moves on */
void sys_poll(void) {
    poll_cycles += 30;
    if (poll_cycles >= 1232) {
        poll_cycles -= 1232;
        if (cpu_ptr) sys_step_line(cpu_ptr, 0); /* the registers of the running code are not stored here: no interrupt now, it waits for a yield */
    }
}

/* a backward branch of the generated code ran out of its budget: some time passes and an interrupt can be taken (the registers are stored) */
int rt_budget = 300;
void rt_yield(Cpu *c) {
    rt_budget = 300;
    sys_step_line(c, 1);
}
