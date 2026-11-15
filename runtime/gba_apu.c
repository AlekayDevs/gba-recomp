/* The sound of the console: the two Direct Sound FIFOs (fed by DMA, drained by a timer) and the four PSG channels
   (two squares, wave, noise). Output: stereo 16-bit at 32768 Hz (one sample every 512 CPU cycles), left in apu_out for
   whoever plays or records it. Clean-room, from the register descriptions of the hardware. */
#include "gba_sys.h"
#include <string.h>

#define OUT_MAX 4096
int16_t apu_out[OUT_MAX * 2];
u32 apu_n; /* stereo frames waiting in apu_out */

void sys_dma_fifo(int x);

static u64 clock_now;   /* cycles at the start of the current line */
static u64 host_clock;  /* cycles of the last sample made */

/* ---- Direct Sound ---- */
static struct { int8_t q[32]; u32 head, cnt; int8_t cur; } fifo[2];

static void fifo_reset(int x) { fifo[x].head = fifo[x].cnt = 0; fifo[x].cur = 0; }
static void fifo_push(int x, u32 b) {
    if (fifo[x].cnt < 32) { fifo[x].q[(fifo[x].head + fifo[x].cnt) & 31] = (int8_t)b; fifo[x].cnt++; }
}
void apu_fifo_write(u32 off, u32 v, int bytes) {
    int x = (off - 0xA0) >> 2 & 1;
    for (int i = 0; i < bytes; i++) fifo_push(x, v >> (8 * i));
}

/* ---- PSG ---- */
typedef struct {
    int on, dac, vol, up, len, len_en;
    u32 env_step, env_t; /* envelope step time in 1/64 s units, cycles into it */
    double ph;
    u32 freq;
    u32 sw_time, sw_shift, sw_t; int sw_dec; /* channel 1 only */
} Sq;
static Sq sq[2];
static struct { int on, dac, len, len_en; u32 freq; double pos; } wv;
static struct { int on, dac, vol, up, len, len_en; u32 env_step, env_t; u32 lfsr; double acc; } nz;
static uint8_t wave_bank[2][16];
static u32 len_acc;

static const uint8_t duty_pat[4] = {0x01, 0x81, 0x87, 0x7E};

static void sq_trigger(int i, u32 cnt_h, u32 cnt_x) {
    Sq *s = &sq[i];
    s->freq = cnt_x & 0x7FF;
    s->len_en = (cnt_x >> 14) & 1;
    s->dac = (cnt_h & 0xF800) != 0;
    s->on = s->dac;
    s->vol = (cnt_h >> 12) & 15;
    s->up = (cnt_h >> 11) & 1;
    s->env_step = (cnt_h >> 8) & 7;
    s->env_t = 0;
    if (s->len == 0) s->len = 64;
    if (i == 0) {
        u32 sw = io_get16(0x60);
        s->sw_shift = sw & 7; s->sw_dec = (sw >> 3) & 1; s->sw_time = (sw >> 4) & 7; s->sw_t = 0;
    }
}

void apu_write16(u32 off, u32 v) {
    switch (off) {
    case 0x62: sq[0].len = 64 - (v & 63); break;
    case 0x68: sq[1].len = 64 - (v & 63); break;
    case 0x78: nz.len = 64 - (v & 63); break;
    case 0x64: case 0x6C: {
        int i = off == 0x6C;
        sq[i].freq = v & 0x7FF; sq[i].len_en = (v >> 14) & 1;
        if (v & 0x8000) sq_trigger(i, io_get16(i ? 0x68 : 0x62), v);
        io_set16(off, v & ~0x8000u);
        break;
    }
    case 0x70: wv.dac = (v >> 7) & 1; if (!wv.dac) wv.on = 0; break;
    case 0x72: wv.len = 256 - (v & 255); break;
    case 0x74:
        wv.freq = v & 0x7FF; wv.len_en = (v >> 14) & 1;
        if (v & 0x8000) { wv.on = wv.dac; if (wv.len == 0) wv.len = 256; wv.pos = 0; }
        io_set16(off, v & ~0x8000u);
        break;
    case 0x7C: {
        nz.len_en = (v >> 14) & 1;
        if (v & 0x8000) {
            u32 e = io_get16(0x78);
            nz.dac = (e & 0xF800) != 0; nz.on = nz.dac;
            nz.vol = (e >> 12) & 15; nz.up = (e >> 11) & 1; nz.env_step = (e >> 8) & 7; nz.env_t = 0;
            nz.lfsr = 0x7FFF; nz.acc = 0;
            if (nz.len == 0) nz.len = 64;
        }
        io_set16(off, v & ~0x8000u);
        break;
    }
    case 0x82:
        if (v & 0x0800) fifo_reset(0);
        if (v & 0x8000) fifo_reset(1);
        io_set16(off, v & ~0x8800u);
        break;
    case 0x84: if (!(v & 0x80)) { sq[0].on = sq[1].on = wv.on = nz.on = 0; } break;
    default:
        if (off >= 0x90 && off < 0xA0) { /* wave RAM: the bank that is not playing */
            int b = !((io_get16(0x70) >> 6) & 1);
            wave_bank[b][off - 0x90] = (uint8_t)v; wave_bank[b][off - 0x90 + 1] = (uint8_t)(v >> 8);
        }
        break;
    }
}

static void env_step(int *vol, int up, u32 step, u32 *t) {
    if (!step) return;
    u32 period = step * 262144;
    *t += 512;
    while (*t >= period) { *t -= period; if (up) { if (*vol < 15) (*vol)++; } else if (*vol > 0) (*vol)--; }
}

/* one 512-cycle step of the PSG; the mix of each side is added to *l and *r */
static void psg_step(int *l, int *r) {
    len_acc += 512;
    while (len_acc >= 65536) {
        len_acc -= 65536;
        for (int i = 0; i < 2; i++) if (sq[i].len_en && sq[i].len && --sq[i].len == 0) sq[i].on = 0;
        if (wv.len_en && wv.len && --wv.len == 0) wv.on = 0;
        if (nz.len_en && nz.len && --nz.len == 0) nz.on = 0;
    }
    int amp[4] = {0, 0, 0, 0};
    for (int i = 0; i < 2; i++) {
        Sq *s = &sq[i];
        if (!s->on) continue;
        env_step(&s->vol, s->up, s->env_step, &s->env_t);
        if (i == 0 && s->sw_time) {
            s->sw_t += 512;
            while (s->sw_t >= s->sw_time * 131072) {
                s->sw_t -= s->sw_time * 131072;
                u32 d = s->freq >> s->sw_shift;
                if (s->sw_dec) { if (s->freq >= d) s->freq -= d; }
                else { s->freq += d; if (s->freq > 2047) { s->on = 0; break; } }
            }
        }
        if (s->freq >= 2048) continue;
        s->ph += 512.0 / (16.0 * (2048 - s->freq));
        u32 step = (u32)s->ph & 7;
        if (s->ph > 1e6) s->ph -= 1e6;
        u32 duty = (io_get16(i ? 0x68 : 0x62) >> 6) & 3;
        amp[i] = (duty_pat[duty] >> step) & 1 ? s->vol : 0;
    }
    if (wv.on) {
        u32 c = io_get16(0x70), n = (c >> 5) & 1 ? 64 : 32;
        wv.pos += 512.0 / (8.0 * (2048 - wv.freq));
        u32 idx = (u32)wv.pos % n;
        int bank = n == 64 ? (int)(idx >> 5) : (int)((c >> 6) & 1);
        u32 b = wave_bank[bank][(idx & 31) >> 1];
        int s4 = (idx & 1) ? (b & 15) : (b >> 4);
        u32 vc = (io_get16(0x72) >> 13) & 3;
        if (io_get16(0x72) & 0x8000) s4 = s4 * 3 / 4;
        else s4 = vc == 0 ? 0 : (vc == 1 ? s4 : (vc == 2 ? s4 >> 1 : s4 >> 2));
        amp[2] = s4;
    }
    if (nz.on) {
        env_step(&nz.vol, nz.up, nz.env_step, &nz.env_t);
        u32 c = io_get16(0x7C), rr = c & 7, ss = (c >> 4) & 15;
        double period = 32.0 * (rr ? rr : 0.5) * (double)(1u << (ss + 1));
        nz.acc += 512.0;
        while (nz.acc >= period) {
            nz.acc -= period;
            u32 x = (nz.lfsr & 1) ^ ((nz.lfsr >> 1) & 1);
            nz.lfsr = (nz.lfsr >> 1) | (x << 14);
            if (c & 8) nz.lfsr = (nz.lfsr & ~0x40u) | (x << 6);
        }
        amp[3] = (nz.lfsr & 1) ? 0 : nz.vol;
    }
    u32 ctl = io_get16(0x80);
    int sum_r = 0, sum_l = 0;
    for (int i = 0; i < 4; i++) {
        if ((ctl >> (8 + i)) & 1) sum_r += amp[i];
        if ((ctl >> (12 + i)) & 1) sum_l += amp[i];
    }
    *r = sum_r * (int)((ctl & 7) + 1);
    *l = sum_l * (int)(((ctl >> 4) & 7) + 1);
}

/* ---- output ---- */
static double dc_x[2], dc_y[2];

static void make_sample(void) {
    int pl = 0, pr = 0;
    u32 x = io_get16(0x84), h = io_get16(0x82);
    int master = (x >> 7) & 1;
    if (master) psg_step(&pl, &pr);
    static const int pct[4] = {1, 2, 4, 4};
    pl = pl * pct[h & 3] / 4; pr = pr * pct[h & 3] / 4;
    int a = fifo[0].cur * ((h & 4) ? 2 : 1), b = fifo[1].cur * ((h & 8) ? 2 : 1);
    int sl = pl + ((h >> 9) & 1 ? a : 0) + ((h >> 13) & 1 ? b : 0);
    int sr = pr + ((h >> 8) & 1 ? a : 0) + ((h >> 12) & 1 ? b : 0);
    if (!master) sl = sr = 0;
    int in[2] = {sl, sr};
    int16_t o[2];
    for (int k = 0; k < 2; k++) {
        double y = in[k] - dc_x[k] + 0.998 * dc_y[k];
        dc_x[k] = in[k]; dc_y[k] = y;
        if (y > 512) y = 512; else if (y < -512) y = -512;
        o[k] = (int16_t)(y * 60.0);
    }
    if (apu_n < OUT_MAX) { apu_out[apu_n * 2] = o[0]; apu_out[apu_n * 2 + 1] = o[1]; apu_n++; }
}

static void render_until(u64 t) {
    while (host_clock + 512 <= t) { make_sample(); host_clock += 512; }
}

/* timer t overflowed n times during the next `cycles` cycles */
void apu_timer_overflow(int t, u32 n, u32 cycles) {
    u32 h = io_get16(0x82);
    for (u32 i = 0; i < n; i++) {
        render_until(clock_now + (u64)(i + 1) * cycles / n);
        for (int x = 0; x < 2; x++) {
            if (((h >> (10 + 4 * x)) & 1) != (u32)t) continue;
            if (fifo[x].cnt) { fifo[x].cur = fifo[x].q[fifo[x].head]; fifo[x].head = (fifo[x].head + 1) & 31; fifo[x].cnt--; }
            if (fifo[x].cnt <= 16) sys_dma_fifo(x);
        }
    }
}

void apu_advance(u32 cycles) {
    render_until(clock_now + cycles);
    clock_now += cycles;
}
