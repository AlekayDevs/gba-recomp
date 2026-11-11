/* The PPU, a scanline at a time: text backgrounds, affine backgrounds, bitmap modes 3-5, sprites (regular and affine), windows,
   alpha blending and brightness. Output: a 240x160 XRGB frame (`ppu_frame`). Written from the hardware description, not from another emulator. */
#include "gba_sys.h"
#include <stdio.h>
#include <string.h>

uint32_t ppu_frame[160][240];

#define TRANSPARENT 0x8000u /* bit 15 of a layer pixel: nothing here */

static uint16_t bg_line[4][240];
static uint16_t obj_col[240];
static uint8_t obj_pri[240], obj_semi[240], obj_win[240];
static int32_t aff_x[2], aff_y[2]; /* the internal reference points of BG2 and BG3 */

static uint32_t pal16(u32 i) { return rt_pal[i * 2] | (rt_pal[i * 2 + 1] << 8); }
static uint32_t to_rgb(u32 c) { return ((c & 31) * 255 / 31) << 16 | (((c >> 5) & 31) * 255 / 31) << 8 | (((c >> 10) & 31) * 255 / 31); }
static u32 reg(u32 off) { return io_get16(off); }

static void load_affine(void) {
    for (int i = 0; i < 2; i++) {
        u32 b = 0x28 + i * 0x10;
        aff_x[i] = (int32_t)((reg(b) | (reg(b + 2) << 16)) << 4) >> 4;
        aff_y[i] = (int32_t)((reg(b + 4) | (reg(b + 6) << 16)) << 4) >> 4;
    }
}

/* VRAM is 96 KB: offsets 0x18000..0x1FFFF mirror 0x10000..0x17FFF */
static inline u32 vram_off(u32 o) { o &= 0x1FFFF; return o >= 0x18000 ? o - 0x8000 : o; }

static void text_bg(int n, int y, u32 dispcnt) {
    (void)dispcnt;
    uint16_t *out = bg_line[n];
    u32 cnt = reg(0x08 + n * 2);
    u32 cbase = ((cnt >> 2) & 3) * 0x4000, sbase = ((cnt >> 8) & 0x1F) * 0x800, size = cnt >> 14;
    int pal256 = (cnt >> 7) & 1;
    u32 hofs = reg(0x10 + n * 4) & 0x1FF, vofs = reg(0x12 + n * 4) & 0x1FF;
    u32 w = (size & 1) ? 512 : 256, h = (size & 2) ? 512 : 256;
    u32 sy = (y + vofs) & (h - 1);
    for (int x = 0; x < 240; x++) {
        u32 sx = (x + hofs) & (w - 1);
        u32 blk = 0;
        if (size == 1) blk = sx >> 8; else if (size == 2) blk = sy >> 8; else if (size == 3) blk = (sx >> 8) + ((sy >> 8) << 1);
        u32 e_off = sbase + blk * 0x800 + (((sy >> 3) & 31) * 32 + ((sx >> 3) & 31)) * 2;
        u32 e = rt_vram[vram_off(e_off)] | (rt_vram[vram_off(e_off + 1)] << 8);
        u32 tx = sx & 7, ty = sy & 7;
        if (e & 0x400) tx = 7 - tx;
        if (e & 0x800) ty = 7 - ty;
        u32 tile = e & 0x3FF, col;
        if (pal256) {
            u32 o = cbase + tile * 64 + ty * 8 + tx;
            col = o < 0x18000 ? rt_vram[o] : 0;
            out[x] = col ? pal16(col) & 0x7FFF : TRANSPARENT;
        } else {
            u32 o = cbase + tile * 32 + ty * 4 + (tx >> 1);
            col = o < 0x18000 ? rt_vram[o] : 0;
            col = (tx & 1) ? col >> 4 : col & 15;
            out[x] = col ? pal16(((e >> 12) & 15) * 16 + col) & 0x7FFF : TRANSPARENT;
        }
    }
}

static void affine_bg(int n, u32 dispcnt) {
    (void)dispcnt;
    uint16_t *out = bg_line[n];
    int k = n - 2;
    u32 cnt = reg(0x08 + n * 2), pb = 0x20 + k * 0x10;
    u32 cbase = ((cnt >> 2) & 3) * 0x4000, sbase = ((cnt >> 8) & 0x1F) * 0x800, size = cnt >> 14;
    int wrap = (cnt >> 13) & 1;
    int32_t pa = (int16_t)reg(pb), pc = (int16_t)reg(pb + 4);
    int32_t dim = 128 << size;
    int32_t x = aff_x[k], y = aff_y[k];
    for (int i = 0; i < 240; i++, x += pa, y += pc) {
        int32_t tx = x >> 8, ty = y >> 8;
        if (tx < 0 || tx >= dim || ty < 0 || ty >= dim) {
            if (!wrap) { out[i] = TRANSPARENT; continue; }
            tx &= dim - 1; ty &= dim - 1;
        }
        u32 e_off = sbase + (ty >> 3) * (dim >> 3) + (tx >> 3);
        u32 tile = rt_vram[vram_off(e_off)];
        u32 o = cbase + tile * 64 + (ty & 7) * 8 + (tx & 7);
        u32 col = o < 0x18000 ? rt_vram[o] : 0;
        out[i] = col ? pal16(col) & 0x7FFF : TRANSPARENT;
    }
}

static void bitmap_bg(int mode, int y, u32 dispcnt) {
    uint16_t *out = bg_line[2];
    u32 frame = (dispcnt & 0x10) ? 0xA000 : 0;
    for (int x = 0; x < 240; x++) {
        u32 c = TRANSPARENT;
        if (mode == 3) {
            u32 o = (y * 240 + x) * 2;
            c = (rt_vram[o] | (rt_vram[o + 1] << 8)) & 0x7FFF;
        } else if (mode == 4) {
            u32 idx = rt_vram[frame + y * 240 + x];
            if (idx) c = pal16(idx) & 0x7FFF;
        } else if (x < 160 && y < 128) {
            u32 o = frame + (y * 160 + x) * 2;
            c = (rt_vram[o] | (rt_vram[o + 1] << 8)) & 0x7FFF;
        }
        out[x] = (uint16_t)c;
    }
}

static const uint8_t obj_w[4][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}, {0, 0, 0, 0}};
static const uint8_t obj_h[4][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}, {0, 0, 0, 0}};

static void objs(int y, u32 dispcnt) {
    memset(obj_pri, 255, sizeof obj_pri);
    memset(obj_semi, 0, sizeof obj_semi);
    memset(obj_win, 0, sizeof obj_win);
    for (int x = 0; x < 240; x++) obj_col[x] = TRANSPARENT;
    if (!(dispcnt & 0x1000)) return;
    int mode = dispcnt & 7, one_d = (dispcnt >> 6) & 1;
    u32 tbase = 0x10000;
    for (int i = 0; i < 128; i++) {
        const uint8_t *o = rt_oam + i * 8;
        u32 a0 = o[0] | (o[1] << 8), a1 = o[2] | (o[3] << 8), a2 = o[4] | (o[5] << 8);
        int affine = (a0 >> 8) & 1, dbl = (a0 >> 9) & 1;
        if (!affine && dbl) continue; /* disabled */
        int om = (a0 >> 10) & 3, shape = a0 >> 14, sz = a1 >> 14;
        if (om == 3 || shape == 3) continue;
        int w = obj_w[shape][sz], h = obj_h[shape][sz];
        int bw = w, bh = h; /* the box on screen */
        if (affine && dbl) { bw *= 2; bh *= 2; }
        int oy = a0 & 255, ox = a1 & 511;
        if (ox >= 256) ox -= 512;
        int ry = (y - oy) & 255;
        if (ry >= bh) continue;
        int pal256 = (a0 >> 13) & 1, pri = (a2 >> 10) & 3;
        u32 tile = a2 & 0x3FF, pbank = (a2 >> 12) & 15;
        int32_t pa = 0, pb = 0, pc = 0, pd = 0;
        if (affine) {
            const uint8_t *p = rt_oam + ((a1 >> 9) & 31) * 32;
            pa = (int16_t)(p[6] | (p[7] << 8)); pb = (int16_t)(p[14] | (p[15] << 8));
            pc = (int16_t)(p[22] | (p[23] << 8)); pd = (int16_t)(p[30] | (p[31] << 8));
        }
        for (int bx = 0; bx < bw; bx++) {
            int sx = ox + bx;
            if (sx < 0 || sx >= 240) continue;
            int tx, ty;
            if (affine) {
                int dx = bx - bw / 2, dy = ry - bh / 2;
                tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                ty = ((pc * dx + pd * dy) >> 8) + h / 2;
                if (tx < 0 || tx >= w || ty < 0 || ty >= h) continue;
            } else {
                tx = (a1 & 0x1000) ? w - 1 - bx : bx;
                ty = (a1 & 0x2000) ? h - 1 - ry : ry;
            }
            u32 t;
            if (one_d) t = tile + (ty >> 3) * (w >> 3) * (pal256 ? 2 : 1) + (tx >> 3) * (pal256 ? 2 : 1);
            else t = tile + (ty >> 3) * 32 + (tx >> 3) * (pal256 ? 2 : 1);
            t &= 0x3FF;
            if (mode >= 3 && t < 512) continue;
            u32 col;
            if (pal256) {
                u32 off = tbase + t * 32 + (ty & 7) * 8 + (tx & 7);
                col = off < 0x18000 ? rt_vram[off] : 0;
            } else {
                u32 off = tbase + t * 32 + (ty & 7) * 4 + ((tx & 7) >> 1);
                col = off < 0x18000 ? rt_vram[off] : 0;
                col = (tx & 1) ? col >> 4 : col & 15;
            }
            if (!col) continue;
            if (om == 2) { obj_win[sx] = 1; continue; }
            if (pri < obj_pri[sx]) {
                obj_pri[sx] = (uint8_t)pri;
                obj_semi[sx] = om == 1;
                obj_col[sx] = pal16(256 + (pal256 ? col : pbank * 16 + col)) & 0x7FFF;
            }
        }
    }
}

static int in_win(int x, int y, u32 h, u32 v) {
    int x1 = h >> 8, x2 = h & 255, y1 = v >> 8, y2 = v & 255;
    if (x2 > 240 || x1 > x2) x2 = 240;
    if (y2 > 160 || y1 > y2) y2 = 160;
    return x >= x1 && x < x2 && y >= y1 && y < y2;
}

static u32 blend(u32 a, u32 b, u32 eva, u32 evb) {
    u32 r = 0;
    for (int s = 0; s < 15; s += 5) {
        u32 v = (((a >> s) & 31) * eva + ((b >> s) & 31) * evb) >> 4;
        r |= (v > 31 ? 31 : v) << s;
    }
    return r;
}

void ppu_line(int y) {
    u32 dc = reg(IO_DISPCNT);
    uint32_t *dst = ppu_frame[y];
    if (y == 0) load_affine();
    if (dc & 0x80) { for (int x = 0; x < 240; x++) dst[x] = 0xFFFFFF; goto next; }
    int mode = dc & 7;
    for (int n = 0; n < 4; n++) for (int x = 0; x < 240; x++) bg_line[n][x] = TRANSPARENT;
    if (mode == 0) { for (int n = 0; n < 4; n++) if (dc & (0x100 << n)) text_bg(n, y, dc); }
    else if (mode == 1) {
        for (int n = 0; n < 2; n++) if (dc & (0x100 << n)) text_bg(n, y, dc);
        if (dc & 0x400) affine_bg(2, dc);
    } else if (mode == 2) {
        for (int n = 2; n < 4; n++) if (dc & (0x100 << n)) affine_bg(n, dc);
    } else if (mode <= 5 && (dc & 0x400)) bitmap_bg(mode, y, dc);
    objs(y, dc);

    u32 bldcnt = reg(0x50), bmode = (bldcnt >> 6) & 3, alpha = reg(0x52), bldy = reg(0x54) & 31;
    u32 eva = alpha & 31, evb = (alpha >> 8) & 31;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    if (bldy > 16) bldy = 16;
    int use_win = dc & 0xE000;
    u32 winin = reg(0x48), winout = reg(0x4A);
    u32 backdrop = pal16(0) & 0x7FFF;
    for (int x = 0; x < 240; x++) {
        u32 we = 0x3F;
        if (use_win) {
            if ((dc & 0x2000) && in_win(x, y, reg(0x40), reg(0x44))) we = winin & 0x3F;
            else if ((dc & 0x4000) && in_win(x, y, reg(0x42), reg(0x46))) we = (winin >> 8) & 0x3F;
            else if ((dc & 0x8000) && obj_win[x]) we = (winout >> 8) & 0x3F;
            else we = winout & 0x3F;
        }
        /* the two topmost layers: id 0-3 = BG, 4 = OBJ, 5 = backdrop */
        int l1 = 5, l2 = 5;
        u32 c1 = backdrop, c2 = backdrop;
        int found = 0;
        for (int p = 0; p < 4 && found < 2; p++) {
            for (int l = 4; l >= 0 && found < 2; l--) {
                /* within a priority the OBJ comes first, then BG0..BG3: visit OBJ (4) then 0..3 */
                int layer = l == 4 ? 4 : 3 - l;
                if (layer == 4 ? 0 : 0) {}
                int lay = (l == 4) ? 4 : (3 - l);
                (void)layer;
                u32 col;
                if (lay == 4) {
                    if (!(we & 0x10) || obj_pri[x] != p || (obj_col[x] & TRANSPARENT)) continue;
                    col = obj_col[x];
                } else {
                    if (!(we & (1 << lay)) || !(dc & (0x100 << lay))) continue;
                    if (((reg(0x08 + lay * 2)) & 3) != (u32)p || (bg_line[lay][x] & TRANSPARENT)) continue;
                    col = bg_line[lay][x];
                }
                if (found == 0) { l1 = lay; c1 = col; } else { l2 = lay; c2 = col; }
                found++;
            }
        }
        (void)l2;
        u32 out = c1;
        if (we & 0x20) {
            u32 first = (bldcnt >> l1) & 1, second = (bldcnt >> (8 + l2)) & 1;
            if (l1 == 4 && obj_semi[x] && second) out = blend(c1, c2, eva, evb);
            else if (bmode == 1 && first && second) out = blend(c1, c2, eva, evb);
            else if (bmode == 2 && first) out = c1 + ((((31 - (c1 & 31)) * bldy) >> 4)) + ((((31 - ((c1 >> 5) & 31)) * bldy) >> 4) << 5) + ((((31 - ((c1 >> 10) & 31)) * bldy) >> 4) << 10);
            else if (bmode == 3 && first) out = (c1 & 31) - (((c1 & 31) * bldy) >> 4) + ((((c1 >> 5) & 31) - ((((c1 >> 5) & 31) * bldy) >> 4)) << 5) + ((((c1 >> 10) & 31) - ((((c1 >> 10) & 31) * bldy) >> 4)) << 10);
        }
        dst[x] = to_rgb(out);
    }
next:
    /* the affine reference points move by (pb, pd) after every line */
    for (int k = 0; k < 2; k++) {
        aff_x[k] += (int16_t)reg(0x22 + k * 0x10);
        aff_y[k] += (int16_t)reg(0x26 + k * 0x10);
    }
}

void ppu_frame_done(void) {}

int ppu_write_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fprintf(f, "P6\n240 160\n255\n");
    for (int y = 0; y < 160; y++)
        for (int x = 0; x < 240; x++) {
            uint32_t c = ppu_frame[y][x];
            fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
        }
    fclose(f);
    return 1;
}
