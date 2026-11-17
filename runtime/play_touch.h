#include <math.h>
/* Touch controls for the Android player, drawn over the picture: a D-pad, A and B, L and R, Start and Select, and three small
   buttons (SAVE and LOAD of the current slot, REWIND held). Included by play.c on Android only. Clean-room: own layout and drawing.
   The layout follows the size of the window (u = a sixth of the shorter side); fingers are tracked by id, so several buttons work at once. */

typedef struct { const char *label; int shape; /* 0 circle, 1 rectangle, 2 d-pad */ float cx, cy, w, h; u32 mask; SDL_Texture *tex; int lw, lh; } TouchBtn;
enum { TB_DPAD, TB_A, TB_B, TB_L, TB_R, TB_START, TB_SELECT, TB_SAVE, TB_LOAD, TB_REWIND, TB_MENU, TB_N };
static TouchBtn tbtn[TB_N] = {
    {"", 2, 0, 0, 0, 0, 0, NULL, 0, 0},       {"A", 0, 0, 0, 0, 0, 1u << 0, NULL, 0, 0}, {"B", 0, 0, 0, 0, 0, 1u << 1, NULL, 0, 0},
    {"L", 1, 0, 0, 0, 0, 1u << 9, NULL, 0, 0}, {"R", 1, 0, 0, 0, 0, 1u << 8, NULL, 0, 0}, {"START", 1, 0, 0, 0, 0, 1u << 3, NULL, 0, 0},
    {"SELECT", 1, 0, 0, 0, 0, 1u << 2, NULL, 0, 0}, {"SAVE", 1, 0, 0, 0, 0, 0, NULL, 0, 0}, {"LOAD", 1, 0, 0, 0, 0, 0, NULL, 0, 0},
    {"REWIND", 1, 0, 0, 0, 0, 0, NULL, 0, 0}, {"MENU", 1, 0, 0, 0, 0, 0, NULL, 0, 0},
};
static int touch_menu_req; /* the MENU button was tapped: play.c opens the menu */
static struct { SDL_FingerID id; float x, y; int on; } tfinger[10];
static Uint32 touch_last_ms; /* when the screen was last touched (the buttons hide while a controller is in use) */
void gba_haptic(void); /* android_main.c: a short vibration */
static int touch_rewind, touch_seen; /* touch_seen: the screen has been touched at least once (the buttons fade when a controller is used) */
static int tw, th;
static float tu;

static void touch_layout(void) {
    SDL_GetRendererOutputSize(ren, &tw, &th);
    tu = (tw < th ? tw : th) / 8.5f * cfg.t_size / 100.0f;
    TouchBtn *b = tbtn;
    int L = cfg.t_left; /* left-handed: the pad on the right, A and B on the left */
    b[TB_DPAD].cx = (L ? tw - 2.05f * tu : 2.05f * tu); b[TB_DPAD].cy = th - 2.0f * tu; b[TB_DPAD].w = b[TB_DPAD].h = 1.55f * tu; /* half-size of the pad */
    b[TB_A].cx = L ? 1.3f * tu : tw - 1.3f * tu; b[TB_A].cy = th - 2.7f * tu; b[TB_A].w = b[TB_A].h = 0.85f * tu; /* radius */
    b[TB_B].cx = L ? 2.9f * tu : tw - 2.9f * tu; b[TB_B].cy = th - 1.4f * tu; b[TB_B].w = b[TB_B].h = 0.85f * tu;
    b[TB_L].cx = 1.4f * tu; b[TB_L].cy = 0.55f * tu; b[TB_L].w = 2.2f * tu; b[TB_L].h = 0.9f * tu;
    b[TB_R].cx = tw - 1.4f * tu; b[TB_R].cy = 0.55f * tu; b[TB_R].w = 2.2f * tu; b[TB_R].h = 0.9f * tu;
    b[TB_SELECT].cx = tw / 2.0f - 1.1f * tu; b[TB_SELECT].cy = th - 0.45f * tu; b[TB_SELECT].w = 1.8f * tu; b[TB_SELECT].h = 0.7f * tu;
    b[TB_START].cx = tw / 2.0f + 1.1f * tu; b[TB_START].cy = th - 0.45f * tu; b[TB_START].w = 1.8f * tu; b[TB_START].h = 0.7f * tu;
    { /* MENU, SAVE, LOAD and REWIND in a row between L and R; when the screen is narrow (portrait) the row goes under them */
        static const int order[4] = {TB_MENU, TB_SAVE, TB_LOAD, TB_REWIND};
        static const float wd[4] = {1.5f, 1.5f, 1.5f, 2.0f};
        float ub = tu * 100.0f / cfg.t_size; /* the row keeps its size when the other buttons grow */
        float gap = 0.4f * ub, total = 3 * gap, x;
        for (int k = 0; k < 4; k++) total += wd[k] * ub;
        int wide = tw >= total + 6.0f * tu;
        x = tw / 2.0f - total / 2;
        for (int k = 0; k < 4; k++) {
            TouchBtn *q = &b[order[k]];
            q->w = wd[k] * ub; q->h = 0.6f * ub; q->cx = x + q->w / 2; q->cy = (wide ? 0.4f : 1.55f) * ub;
            x += q->w + gap;
        }
    }
}

/* ---- pixel art: every control is a small sprite (the size of the console's own pixels) drawn once into a texture and scaled up by a
   whole factor with no smoothing, so it stays crisp. The look follows The Minish Cap: forest green plates with a bevelled gold frame,
   gem-coloured A and B, and Ezlo's cap in the middle of the pad. All the art is original and drawn here in code. ---- */
typedef struct { int w, h; uint8_t *px; } TImg;
static TImg timg_new(int w, int h) { TImg im; im.w = w; im.h = h; im.px = (uint8_t *)calloc((size_t)w * h, 4); return im; }
static void timg_put(TImg *im, int x, int y, uint32_t rgb) {
    if (!im->px || x < 0 || y < 0 || x >= im->w || y >= im->h) return;
    uint8_t *p = im->px + ((size_t)y * im->w + x) * 4;
    p[0] = (uint8_t)(rgb >> 16); p[1] = (uint8_t)(rgb >> 8); p[2] = (uint8_t)rgb; p[3] = 255;
}
#define TC_OUTLINE 0x14240Cu
#define TC_GOLD_HI 0xFFF0A0u
#define TC_GOLD    0xF0C840u
#define TC_GOLD_LO 0xB07818u
#define TC_GOLD_DK 0x684010u
#define TC_STONE   0x1E4A30u /* forest green body */
#define TC_STONE_HI 0x2F6E46u
#define TC_PRESS   0x58B868u
#define TC_TEXT    0xFFF8D8u
#define TC_SHADOW  0x0C2014u
static const uint8_t tfont[][7] = {
    /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* S */ {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* N */ {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* D */ {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* V */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
};
static const uint8_t *tglyph(char c) { static const char order[] = "ABLRSTECMNUODVWI"; const char *p = c ? strchr(order, c) : NULL; return p ? tfont[p - order] : NULL; }
static int ttext_w(const char *s) { return (int)strlen(s) * 6 - 1; }
static void ttext(TImg *im, int x, int y, const char *s, uint32_t col, uint32_t shadow) {
    for (int pass = 0; pass < 2; pass++) {
        int cx = x + (pass == 0), cy = y + (pass == 0);
        for (const char *c = s; *c; c++, cx += 6) {
            const uint8_t *g = tglyph(*c);
            if (!g) continue;
            for (int gy = 0; gy < 7; gy++) for (int gx = 0; gx < 5; gx++) if (g[gy] & (0x10 >> gx)) timg_put(im, cx + gx, cy + gy, pass == 0 ? shadow : col);
        }
    }
}
typedef struct { int w, h; } TBox;
typedef int (*TInside)(const void *shape, int x, int y, int e);
/* the gold frame: dark outline, a bevel that catches the light at the top left, a thin dark line and then the body */
static void tplate(TImg *im, TInside inside, const void *shape, uint32_t body, uint32_t body_hi, int pressed) {
    for (int y = 0; y < im->h; y++) for (int x = 0; x < im->w; x++) {
        int lit = (x + y) < (im->w + im->h) / 2;
        uint32_t hi = pressed ? TC_GOLD : TC_GOLD_HI, mid = pressed ? TC_GOLD_LO : TC_GOLD, lo = pressed ? TC_GOLD_DK : TC_GOLD_LO;
        if (!inside(shape, x, y, 0)) continue;
        if (!inside(shape, x, y, 1)) timg_put(im, x, y, TC_OUTLINE);
        else if (!inside(shape, x, y, 2)) timg_put(im, x, y, lit ? hi : mid);
        else if (!inside(shape, x, y, 3)) timg_put(im, x, y, lit ? mid : lo);
        else if (!inside(shape, x, y, 4)) timg_put(im, x, y, TC_GOLD_DK);
        else timg_put(im, x, y, (x + y) * 100 / (im->w + im->h) < 38 && !pressed ? body_hi : body);
    }
}
static int tin_round(const void *shape, int x, int y, int e) {
    const TBox *b = (const TBox *)shape;
    float r = b->h / 2.0f - e, dx = (float)x - (b->w - 1) / 2.0f, dy = (float)y - (b->h - 1) / 2.0f, half = (b->w - b->h) / 2.0f;
    if (r < 0) return 0;
    dx = dx > half ? dx - half : dx < -half ? dx + half : 0;
    return dx * dx + dy * dy <= (r + 0.5f) * (r + 0.5f);
}
static int tin_plate(const void *shape, int x, int y, int e) {
    const TBox *b = (const TBox *)shape;
    int c = 2 - e > 0 ? 2 - e : 0;
    if (x < e || y < e || x >= b->w - e || y >= b->h - e) return 0;
    if (x < e + c && y < e + c && (x - e) + (y - e) < c) return 0;
    if (x >= b->w - e - c && y < e + c && (b->w - 1 - e - x) + (y - e) < c) return 0;
    if (x < e + c && y >= b->h - e - c && (x - e) + (b->h - 1 - e - y) < c) return 0;
    if (x >= b->w - e - c && y >= b->h - e - c && (b->w - 1 - e - x) + (b->h - 1 - e - y) < c) return 0;
    return 1;
}
/* A and B: a gem set in the gold ring (emerald and sapphire, like the rupees), with a shine at the top left */
static TImg spr_button(int isb, int pressed) {
    TBox b = {27, 27};
    TImg im = timg_new(b.w, b.h);
    uint32_t col = isb ? 0x2858C8u : 0x2E9E48u, hi = isb ? 0x78A0F0u : 0x78E090u;
    if (pressed) col = (col >> 1) & 0x7F7F7Fu;
    tplate(&im, tin_round, &b, col, hi, pressed);
    if (!pressed) { timg_put(&im, 7, 7, 0xFFFFFFu); timg_put(&im, 8, 7, 0xFFFFFFu); timg_put(&im, 7, 8, 0xFFFFFFu); }
    ttext(&im, (b.w - 5) / 2 + pressed, (b.h - 7) / 2 + pressed, isb ? "B" : "A", TC_TEXT, TC_SHADOW);
    return im;
}
static TImg spr_pill(int w, int h, const char *label, int pressed, int round) {
    TBox b = {w, h};
    TImg im = timg_new(w, h);
    tplate(&im, round ? tin_round : tin_plate, &b, pressed ? TC_PRESS : TC_STONE, TC_STONE_HI, pressed);
    ttext(&im, (w - ttext_w(label)) / 2 + pressed, (h - 7) / 2 + pressed, label, TC_GOLD_HI, TC_SHADOW);
    return im;
}
static int tcross(int dx, int dy, int a, int l, int e) {
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    a -= e; l -= e;
    return (ax <= a && ay <= l) || (ay <= a && ax <= l);
}
/* Ezlo's cap: a green pointed hat bent to one side with a gold band and a dark outline so it stands out on the pad, 13 by 11 */
static void that(TImg *im, int ox, int oy) {
    static const char *m[11] = {
        "......gg.....", ".....gGgg....", "....gGgggg...", "...gGggggg...", "..gGgggggg...", ".gGggggggg...", ".gGgggggggg..",
        "gGgggggggggg.", "bBBBBBBBBBBb.", "bbbbbbbbbbbb.", ".bbbbbbbbbb..",
    };
    for (int pass = 0; pass < 2; pass++)
        for (int y = -1; y <= 11; y++) for (int x = -1; x <= 13; x++) {
            char c = (y >= 0 && y < 11 && x >= 0 && x < 13) ? m[y][x] : '.';
            if (pass == 0) {
                if (c != '.') continue;
                int near = 0;
                for (int d = 0; d < 4; d++) {
                    int yy = y + (d == 0) - (d == 1), xx = x + (d == 2) - (d == 3);
                    if (yy >= 0 && yy < 11 && xx >= 0 && xx < 13 && m[yy][xx] != '.') near = 1;
                }
                if (near) timg_put(im, ox + x, oy + y, TC_OUTLINE);
            } else if (c != '.') {
                uint32_t col = c == 'g' ? 0x3CC060u : c == 'G' ? 0xA0F0A0u : c == 'b' ? TC_GOLD_LO : TC_GOLD_HI;
                timg_put(im, ox + x, oy + y, col);
            }
        }
}
/* the pad: a green cross in a gold frame, an arrow on each arm and the cap in the middle; dir 0 is the plate, 1..4 the lit arm (up down left right) */
static TImg spr_dpad(int dir) {
    int n = 65, c = 32;
    TImg im = timg_new(n, n);
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        int dx = x - c, dy = y - c, lit = dx + dy < 0, arm = 0, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
        if (dir == 1) arm = dy < 0 && tcross(dx, dy, 9, 31, 4) && adx <= 5;
        else if (dir == 2) arm = dy > 0 && tcross(dx, dy, 9, 31, 4) && adx <= 5;
        else if (dir == 3) arm = dx < 0 && tcross(dx, dy, 9, 31, 4) && ady <= 5;
        else if (dir == 4) arm = dx > 0 && tcross(dx, dy, 9, 31, 4) && ady <= 5;
        if (dir) { if (arm) { timg_put(&im, x, y, TC_PRESS); im.px[((size_t)y * n + x) * 4 + 3] = 170; } continue; }
        if (!tcross(dx, dy, 10, 32, 0)) continue;
        if (!tcross(dx, dy, 10, 32, 1)) timg_put(&im, x, y, TC_OUTLINE);
        else if (!tcross(dx, dy, 10, 32, 3)) timg_put(&im, x, y, lit ? TC_GOLD_HI : TC_GOLD);
        else if (!tcross(dx, dy, 10, 32, 4)) timg_put(&im, x, y, lit ? TC_GOLD : TC_GOLD_LO);
        else if (!tcross(dx, dy, 10, 32, 5)) timg_put(&im, x, y, TC_GOLD_DK);
        else timg_put(&im, x, y, lit && dx + dy < -10 ? TC_STONE_HI : TC_STONE);
    }
    if (!dir) {
        for (int i = 0; i < 4; i++) {
            int ax = i == 2 ? -24 : i == 3 ? 24 : 0, ay = i == 0 ? -24 : i == 1 ? 24 : 0;
            for (int k = 0; k < 4; k++) for (int w = -k; w <= k; w++)
                timg_put(&im, c + (i < 2 ? ax + w : ax + (i == 2 ? 3 - k : k - 3)), c + (i < 2 ? ay + (i == 0 ? 3 - k : k - 3) : ay + w), TC_GOLD);
        }
        that(&im, c - 6, c - 5);
    }
    return im;
}
static SDL_Texture *timg_tex(TImg *im) {
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (t) { SDL_UpdateTexture(t, NULL, im->px, im->w * 4); SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest); }
    free(im->px); im->px = NULL;
    return t;
}
static SDL_Texture *tspr[TB_N][2]; /* [control][up/down]; the pad's arms are in tarm */
static SDL_Texture *tarm[4];
static int tspr_w[TB_N];
static void touch_art(void) {
    static const char *lab[TB_N] = {"", "A", "B", "L", "R", "START", "SELECT", "SAVE", "LOAD", "REWIND", "MENU"};
    for (int i = 0; i < TB_N; i++) for (int d = 0; d < 2; d++) {
        TImg im;
        if (i == TB_DPAD) { if (d) continue; im = spr_dpad(0); }
        else if (i == TB_A || i == TB_B) im = spr_button(i == TB_B, d);
        else if (i == TB_L || i == TB_R) im = spr_pill(41, 17, lab[i], d, 1);
        else im = spr_pill(i == TB_START || i == TB_SELECT ? 45 : 39, 13, lab[i], d, 0);
        tspr_w[i] = im.w;
        tspr[i][d] = timg_tex(&im);
    }
    for (int k = 0; k < 4; k++) { TImg im = spr_dpad(k + 1); tarm[k] = timg_tex(&im); }
}

static void touch_init(void) {
    touch_art();
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    touch_layout();
}

static int touch_hit(const TouchBtn *b, float x, float y) {
    if (!cfg.t_quick && (b == &tbtn[TB_SAVE] || b == &tbtn[TB_LOAD] || b == &tbtn[TB_REWIND])) return 0; /* hidden in the menu */
    float dx = x - b->cx, dy = y - b->cy;
    if (b->shape == 0) return dx * dx + dy * dy <= b->w * b->w * 1.3f; /* a little generous: thumbs are big */
    if (b->shape == 2) return dx > -b->w && dx < b->w && dy > -b->h && dy < b->h;
    return dx > -b->w / 2 - 0.2f * tu && dx < b->w / 2 + 0.2f * tu && dy > -b->h / 2 - 0.2f * tu && dy < b->h / 2 + 0.2f * tu;
}

/* the key bits of the buttons under one point; *rew is set when it is on REWIND */
static u32 touch_bits(float x, float y, int *rew) {
    u32 r = 0;
    for (int i = 0; i < TB_N; i++) {
        const TouchBtn *b = &tbtn[i];
        if (!touch_hit(b, x, y)) continue;
        if (i == TB_DPAD) {
            float dx = x - b->cx, dy = y - b->cy, t = 0.3f * b->w;
            if (dx > t) r |= 16;
            if (dx < -t) r |= 32;
            if (dy < -t) r |= 64;
            if (dy > t) r |= 128;
        } else if (i == TB_REWIND) { if (rew) *rew = 1; }
        else r |= b->mask;
    }
    return r;
}

/* a quick tap can go down and up inside one frame: what a finger touched when it went down stays pressed for that frame */
static u32 touch_latch;

/* the GBA key bits held by the fingers on the screen */
static u32 touch_mask(void) {
    u32 r = touch_latch;
    touch_latch = 0;
    touch_rewind = 0;
    for (int f = 0; f < 10; f++)
        if (tfinger[f].on) r |= touch_bits(tfinger[f].x * tw, tfinger[f].y * th, &touch_rewind);
    return r;
}

/* finger events; SAVE and LOAD act when the finger goes down */
static void touch_event(const SDL_Event *e) {
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION && e->type != SDL_FINGERUP) return;
    touch_seen = 1;
    touch_last_ms = SDL_GetTicks();
    int slot_i = -1, free_i = -1;
    for (int f = 0; f < 10; f++) {
        if (tfinger[f].on && tfinger[f].id == e->tfinger.fingerId) slot_i = f;
        if (!tfinger[f].on && free_i < 0) free_i = f;
    }
    if (e->type == SDL_FINGERUP) { if (slot_i >= 0) tfinger[slot_i].on = 0; return; }
    if (slot_i < 0) slot_i = free_i;
    if (slot_i < 0) return;
    if (e->type == SDL_FINGERDOWN) {
        float x = e->tfinger.x * tw, y = e->tfinger.y * th;
        u32 hit = touch_bits(x, y, NULL);
        touch_latch |= hit;
        if (cfg.t_haptic && (hit || touch_hit(&tbtn[TB_MENU], x, y) || touch_hit(&tbtn[TB_SAVE], x, y) || touch_hit(&tbtn[TB_LOAD], x, y) || touch_hit(&tbtn[TB_REWIND], x, y))) gba_haptic();
        if (touch_hit(&tbtn[TB_MENU], x, y)) touch_menu_req = 1;
        if ((touch_hit(&tbtn[TB_SAVE], x, y) || touch_hit(&tbtn[TB_LOAD], x, y)) && !rt_state_req) {
            snprintf(rt_state_path, sizeof rt_state_path, "%s.s%d", state_base, slot);
            rt_state_req = touch_hit(&tbtn[TB_SAVE], x, y) ? 1 : 2;
        }
    }
    tfinger[slot_i].on = 1; tfinger[slot_i].id = e->tfinger.fingerId; tfinger[slot_i].x = e->tfinger.x; tfinger[slot_i].y = e->tfinger.y;
}


/* ---- drawing: the sprites, scaled by a whole factor ---- */
static void tsprite(SDL_Texture *t, int cx, int cy, int w0, int h0, int target, int alpha) {
    if (!t) return;
    int s = (int)(target / (float)w0 + 0.5f); if (s < 1) s = 1;
    SDL_Rect d = {cx - w0 * s / 2, cy - h0 * s / 2, w0 * s, h0 * s};
    SDL_SetTextureAlphaMod(t, (uint8_t)alpha);
    SDL_RenderCopy(ren, t, NULL, &d);
}

static void touch_draw(u32 held) {
    touch_layout();
    if (cfg.t_hide && pad && SDL_GetTicks() - touch_last_ms > 4000) return; /* a controller is in use: no buttons on the screen */
    SDL_RenderSetLogicalSize(ren, 0, 0);
    for (int i = 0; i < TB_N; i++) {
        TouchBtn *b = &tbtn[i];
        int down = 0;
        if (!cfg.t_quick && (i == TB_SAVE || i == TB_LOAD || i == TB_REWIND)) continue;
        if (i == TB_DPAD) down = (held & 0xF0) != 0;
        else if (i == TB_REWIND) down = touch_rewind;
        else if (b->mask) down = (held & b->mask) != 0;
        int small = i >= TB_SAVE, a = (down ? 255 : (small ? 105 : 205)) * cfg.t_opacity / 100;
        if (i == TB_DPAD) {
            int target = (int)(2 * b->w);
            tsprite(tspr[i][0], (int)b->cx, (int)b->cy, 65, 65, target, a);
            static const u32 bit[4] = {64, 128, 32, 16};
            for (int k = 0; k < 4; k++) if (held & bit[k]) tsprite(tarm[k], (int)b->cx, (int)b->cy, 65, 65, target, 255);
            continue;
        }
        int w0 = tspr_w[i], h0 = (i == TB_A || i == TB_B) ? 27 : (i == TB_L || i == TB_R) ? 17 : 13;
        int target = b->shape == 0 ? (int)(2 * b->w) : (int)b->w;
        tsprite(tspr[i][down], (int)b->cx, (int)b->cy, w0, h0, target, a);
    }
    SDL_RenderSetLogicalSize(ren, 240, 160);
}
