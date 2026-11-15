/* Save states. The CPU of the recompiled game is the C call stack, which cannot be stored; so a state is taken and loaded
   only at one place of the game's main loop (the profile's [resume] address), where the whole CPU is in `Cpu` and the rest is
   memory and the little state of the other modules. The generated code calls rt_state_point there when a request is pending.
   File: magic, the game's id (header bytes), the sizes, then the blobs in a fixed order. The game's own save (EEPROM) is not
   part of a state: loading one does not undo a save made in the game. */
#include "gba_sys.h"
#include <stdio.h>
#include <string.h>

volatile int rt_state_req;       /* 0 nothing, 1 save, 2 load */
char rt_state_path[512];
const char *rt_state_msg;        /* result of the last request, for whoever shows messages */

void st_blob(StateIO *s, void *p, size_t n) {
    if (s->err) return;
    if (s->save) { if (fwrite(p, 1, n, s->f) != n) s->err = 1; }
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

void rt_state_point(Cpu *c) {
    int req = rt_state_req;
    rt_state_req = 0;
    StateIO s = {0};
    s.save = req == 1;
    s.f = fopen(rt_state_path, s.save ? "wb" : "rb");
    if (!s.f) { rt_state_msg = s.save ? "cannot write the state" : "no state there"; return; }
    char m[8];
    if (s.save) fwrite(magic, 1, 8, s.f);
    else if (fread(m, 1, 8, s.f) != 8 || memcmp(m, magic, 8)) { fclose(s.f); rt_state_msg = "not a state of this program"; return; }
    if (s.save) { all(&s, c); }
    else {
        Cpu keep = *c;
        all(&s, c);
        if (s.err) *c = keep; /* a bad file leaves the CPU as it was (memory may be half loaded: reload again or reset) */
    }
    fclose(s.f);
    if (s.err == 2) rt_state_msg = "the state is from another game";
    else if (s.err) rt_state_msg = s.save ? "write error" : "the state file is damaged";
    else rt_state_msg = s.save ? "state saved" : "state loaded";
    if (!s.save && !s.err) apu_n = 0;
}
