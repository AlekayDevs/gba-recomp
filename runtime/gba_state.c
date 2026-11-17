/* Save states. The CPU of the recompiled game is the C call stack, which cannot be stored; so a state is taken and loaded
   only at one place of the game's main loop (the profile's [resume] address), where the whole CPU is in `Cpu` and the rest is
   memory and the little state of the other modules. The generated code calls rt_state_point there when a request is pending.
   File: magic, the game's id (header bytes), the sizes, then the blobs in a fixed order. The game's own save (EEPROM) is not
   part of a state: loading one does not undo a save made in the game. */
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

volatile int rt_state_req;       /* 0 nothing, 1 save, 2 load */
char rt_state_path[512];
const char *rt_state_msg;        /* result of the last request, for whoever shows messages */

void st_blob(StateIO *s, void *p, size_t n) {
    if (s->err) return;
    if (s->buf) {
        if (s->pos + n > s->cap) { s->err = 1; return; }
        if (s->save) memcpy(s->buf + s->pos, p, n); else memcpy(p, s->buf + s->pos, n);
        s->pos += n;
    }
    else if (s->save) { if (fwrite(p, 1, n, s->f) != n) s->err = 1; }
    else if (fread(p, 1, n, s->f) != n) s->err = 1;
}

void sys_state(StateIO *s);
void ppu_state(StateIO *s);
void apu_state(StateIO *s);
void save_state_io(StateIO *s);

static const char magic[8] = {'G', 'B', 'A', 'R', 'S', 1, 0, 0};

static void all(StateIO *s, Cpu *c) {
    char id[0x20];
    if (s->save) { memcpy(id, rt_rom + 0xA0, 0x20); }
    st_blob(s, id, sizeof id);
    if (!s->save && memcmp(id, rt_rom + 0xA0, 0x20)) { s->err = 2; return; }
    st_blob(s, c, sizeof *c);
    st_blob(s, rt_ewram, sizeof rt_ewram);
    st_blob(s, rt_iwram, sizeof rt_iwram);
    st_blob(s, rt_pal, sizeof rt_pal);
    st_blob(s, rt_vram, sizeof rt_vram);
    st_blob(s, rt_oam, sizeof rt_oam);
    st_blob(s, rt_io, sizeof rt_io);
    sys_state(s);
    ppu_state(s);
    apu_state(s);
    save_state_io(s);
}

/* ---- the rewind: a history of states in memory ----
   Each push serializes the machine (the same blobs as a file) and stores the difference with the previous one (XOR, with the
   runs of zeros squeezed out): between two moments a few frames apart almost nothing changes, so a step costs a few KB.
   `cur` is the newest state in full; going back XORs the last difference into it and loads the result. */
#define STATE_MAX (640 * 1024)
#define HIST_MAX 1200                 /* steps kept */
#define HIST_BYTES (96u << 20)        /* and no more than this much memory */
int rt_rewind_verify;
static uint8_t cur[STATE_MAX], tmp[STATE_MAX];
static size_t cur_len;
static struct { uint8_t *d; size_t n; uint32_t hash; } hist[HIST_MAX]; /* hash: of the state this step goes back to (rt_rewind_verify only) */
static uint32_t fnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; while (n--) h = (h ^ *p++) * 16777619u; return h; }
static int h_first, h_count;          /* a ring: the oldest entry and how many */
static size_t h_bytes;

static void hist_drop_oldest(void) {
    free(hist[h_first].d); h_bytes -= hist[h_first].n; hist[h_first].d = NULL;
    h_first = (h_first + 1) % HIST_MAX; h_count--;
}

void rt_rewind_clear(void) {
    while (h_count) hist_drop_oldest();
    cur_len = 0;
}

int rt_rewind_steps(void) { return h_count; }

/* XOR delta of two buffers of n bytes, as (zero run, literal run, literals) with 3-byte lengths each. Returns the size, or 0 if it does not fit in `cap`. */
static size_t delta_pack(const uint8_t *a, const uint8_t *b, size_t n, uint8_t *out, size_t cap) {
    size_t i = 0, o = 0;
    while (i < n) {
        size_t z = i, l;
        while (z < n && a[z] == b[z]) z++;
        size_t e = z;
        while (e < n && (a[e] != b[e] || (e + 8 < n && (a[e + 1] != b[e + 1] || a[e + 2] != b[e + 2] || a[e + 3] != b[e + 3])))) e++; /* a literal run stops at 4 equal bytes */
        l = e - z;
        if (o + 6 + l > cap) return 0;
        out[o++] = (uint8_t)((z - i) >> 16); out[o++] = (uint8_t)((z - i) >> 8); out[o++] = (uint8_t)(z - i);
        out[o++] = (uint8_t)(l >> 16); out[o++] = (uint8_t)(l >> 8); out[o++] = (uint8_t)l;
        for (size_t k = 0; k < l; k++) out[o++] = a[z + k] ^ b[z + k];
        i = e;
    }
    return o;
}

/* a ^= delta (the inverse of delta_pack: the other state comes out of one) */
static void delta_apply(uint8_t *a, size_t n, const uint8_t *d, size_t dn) {
    size_t i = 0, o = 0;
    while (o + 6 <= dn && i < n) {
        size_t z = (size_t)d[o] << 16 | (size_t)d[o + 1] << 8 | d[o + 2], l = (size_t)d[o + 3] << 16 | (size_t)d[o + 4] << 8 | d[o + 5];
        o += 6; i += z;
        for (size_t k = 0; k < l && i < n && o < dn; k++) a[i++] ^= d[o++];
    }
}

/* the machine into / out of a memory buffer; returns the length or 0 */
static size_t mem_save(Cpu *c, uint8_t *buf) {
    StateIO s = {0};
    s.save = 1; s.buf = buf; s.cap = STATE_MAX;
    all(&s, c);
    return s.err ? 0 : s.pos;
}

static int mem_load(Cpu *c, uint8_t *buf, size_t len) {
    StateIO s = {0};
    Cpu keep = *c;
    s.buf = buf; s.cap = len;
    all(&s, c);
    if (s.err) { *c = keep; return 0; }
    apu_n = 0;
    return 1;
}

static void rewind_push(Cpu *c) {
    size_t n = mem_save(c, tmp);
    if (!n) { rt_state_msg = "the state does not fit in the history"; return; }
    if (cur_len == n) {
        static uint8_t dbuf[STATE_MAX];
        size_t dn = delta_pack(cur, tmp, n, dbuf, sizeof dbuf);
        if (dn) {
            while (h_count == HIST_MAX || (h_count && h_bytes + dn > HIST_BYTES)) hist_drop_oldest();
            int at = (h_first + h_count) % HIST_MAX;
            hist[at].d = malloc(dn);
            if (hist[at].d) { memcpy(hist[at].d, dbuf, dn); hist[at].n = dn; hist[at].hash = rt_rewind_verify ? fnv(cur, n) : 0; h_bytes += dn; h_count++; }
        }
    } else rt_rewind_clear();
    memcpy(cur, tmp, n); cur_len = n;
}

static void rewind_pop(Cpu *c) {
    if (!h_count || !cur_len) { rt_state_msg = "no more history"; return; }
    int at = (h_first + h_count - 1) % HIST_MAX;
    delta_apply(cur, cur_len, hist[at].d, hist[at].n);
    int chain_ok = !rt_rewind_verify || fnv(cur, cur_len) == hist[at].hash; /* the XOR chain gives back the state that was pushed */
    free(hist[at].d); h_bytes -= hist[at].n; hist[at].d = NULL; h_count--;
    if (!mem_load(c, cur, cur_len)) { rt_state_msg = "the history is damaged"; rt_rewind_clear(); return; }
    if (rt_rewind_verify) { /* loading and saving again must give back the same bytes */
        size_t n = mem_save(c, tmp);
        rt_state_msg = !chain_ok ? "REWIND CHAIN MISMATCH" : n == cur_len && !memcmp(tmp, cur, n) ? "rewind verified" : "REWIND RELOAD MISMATCH";
    }
}

void rt_state_point(Cpu *c) {
    int req = rt_state_req;
    rt_state_req = 0;
    if (req == 3) { rewind_push(c); return; }
    if (req == 4) { rewind_pop(c); return; }
    StateIO s = {0};
    s.save = req == 1;
    s.f = fopen(rt_state_path, s.save ? "wb" : "rb");
    if (!s.f) { rt_state_msg = s.save ? "cannot write the state" : "no state there"; return; }
    char m[8];
    if (s.save) fwrite(magic, 1, 8, s.f);
    else if (fread(m, 1, 8, s.f) != 8 || memcmp(m, magic, 8)) { fclose(s.f); rt_state_msg = "not a state of this program"; return; }
    if (s.save) { all(&s, c); }
    else {
        /* Stage and validate the whole file before touching RAM or device state. mem_save also gives the expected size. */
        uint8_t *loaded = malloc(STATE_MAX);
        size_t n = loaded ? mem_save(c, loaded) : 0;
        if (!n) { free(loaded); fclose(s.f); rt_state_msg = "cannot prepare the state load"; return; }
        if (fread(loaded, 1, n, s.f) != n) s.err = 1;
        else if (memcmp(loaded, rt_rom + 0xA0, 0x20)) s.err = 2;
        else if (!mem_load(c, loaded, n)) s.err = 1;
        free(loaded);
    }
    fclose(s.f);
    if (s.err == 2) rt_state_msg = "the state is from another game";
    else if (s.err) rt_state_msg = s.save ? "write error" : "the state file is damaged";
    else rt_state_msg = s.save ? "state saved" : "state loaded";
    if (!s.save && !s.err) { apu_n = 0; rt_rewind_clear(); }
}
