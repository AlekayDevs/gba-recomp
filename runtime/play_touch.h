#include <math.h>
/* Touch controls for the Android player, drawn over the picture: a D-pad, A and B, L and R, Start and Select, and three small
   buttons (SAVE and LOAD of the current slot, REWIND held). Included by play.c on Android only. Clean-room: own layout and drawing.
   The layout follows the size of the window (u = a sixth of the shorter side); fingers are tracked by id, so several buttons work at once. */

typedef struct { const char *label; int shape; /* 0 circle, 1 rectangle, 2 d-pad */ float cx, cy, w, h; u32 mask; SDL_Texture *tex; int lw, lh; } TouchBtn;
enum { TB_DPAD, TB_A, TB_B, TB_L, TB_R, TB_START, TB_SELECT, TB_SAVE, TB_LOAD, TB_REWIND, TB_N };
static TouchBtn tbtn[TB_N] = {
    {"", 2, 0, 0, 0, 0, 0, NULL, 0, 0},       {"A", 0, 0, 0, 0, 0, 1u << 0, NULL, 0, 0}, {"B", 0, 0, 0, 0, 0, 1u << 1, NULL, 0, 0},
    {"L", 1, 0, 0, 0, 0, 1u << 9, NULL, 0, 0}, {"R", 1, 0, 0, 0, 0, 1u << 8, NULL, 0, 0}, {"START", 1, 0, 0, 0, 0, 1u << 3, NULL, 0, 0},
    {"SELECT", 1, 0, 0, 0, 0, 1u << 2, NULL, 0, 0}, {"SAVE", 1, 0, 0, 0, 0, 0, NULL, 0, 0}, {"LOAD", 1, 0, 0, 0, 0, 0, NULL, 0, 0},
    {"REWIND", 1, 0, 0, 0, 0, 0, NULL, 0, 0},
};
static struct { SDL_FingerID id; float x, y; int on; } tfinger[10];
static int touch_rewind, touch_seen; /* touch_seen: the screen has been touched at least once (the buttons fade when a controller is used) */
static int tw, th;
static float tu;

static void touch_layout(void) {
    SDL_GetRendererOutputSize(ren, &tw, &th);
    tu = (tw < th ? tw : th) / 8.5f;
    TouchBtn *b = tbtn;
    b[TB_DPAD].cx = 1.9f * tu; b[TB_DPAD].cy = th - 2.0f * tu; b[TB_DPAD].w = b[TB_DPAD].h = 1.55f * tu; /* half-size of the pad */
    b[TB_A].cx = tw - 1.3f * tu; b[TB_A].cy = th - 2.7f * tu; b[TB_A].w = b[TB_A].h = 0.85f * tu; /* radius */
    b[TB_B].cx = tw - 2.9f * tu; b[TB_B].cy = th - 1.4f * tu; b[TB_B].w = b[TB_B].h = 0.85f * tu;
    b[TB_L].cx = 1.4f * tu; b[TB_L].cy = 0.55f * tu; b[TB_L].w = 2.2f * tu; b[TB_L].h = 0.9f * tu;
    b[TB_R].cx = tw - 1.4f * tu; b[TB_R].cy = 0.55f * tu; b[TB_R].w = 2.2f * tu; b[TB_R].h = 0.9f * tu;
    b[TB_SELECT].cx = tw / 2.0f - 1.1f * tu; b[TB_SELECT].cy = th - 0.45f * tu; b[TB_SELECT].w = 1.8f * tu; b[TB_SELECT].h = 0.7f * tu;
    b[TB_START].cx = tw / 2.0f + 1.1f * tu; b[TB_START].cy = th - 0.45f * tu; b[TB_START].w = 1.8f * tu; b[TB_START].h = 0.7f * tu;
    b[TB_SAVE].cx = tw / 2.0f - 1.9f * tu; b[TB_SAVE].cy = 0.4f * tu; b[TB_SAVE].w = 1.5f * tu; b[TB_SAVE].h = 0.6f * tu;
    b[TB_LOAD].cx = tw / 2.0f - 0.2f * tu; b[TB_LOAD].cy = 0.4f * tu; b[TB_LOAD].w = 1.5f * tu; b[TB_LOAD].h = 0.6f * tu;
    b[TB_REWIND].cx = tw / 2.0f + 1.8f * tu; b[TB_REWIND].cy = 0.4f * tu; b[TB_REWIND].w = 2.0f * tu; b[TB_REWIND].h = 0.6f * tu;
}

static void touch_init(void) {
    for (int i = 1; i < TB_N; i++) {
        const char *s = tbtn[i].label;
        int w = (int)strlen(s) * 6 + 4, h = 12;
        static uint8_t buf[200 * 12 * 3];
        memset(buf, 0, sizeof buf);
        Canvas c = {buf, w, h};
        text_draw(&c, 2, 1, s, th_gold, 0);
        SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STATIC, w, h);
        if (!t) continue;
        SDL_UpdateTexture(t, NULL, buf, w * 3);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
        tbtn[i].tex = t; tbtn[i].lw = w; tbtn[i].lh = h;
    }
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    touch_layout();
}

static int touch_hit(const TouchBtn *b, float x, float y) {
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
        touch_latch |= touch_bits(x, y, NULL);
        if ((touch_hit(&tbtn[TB_SAVE], x, y) || touch_hit(&tbtn[TB_LOAD], x, y)) && !rt_state_req) {
            snprintf(rt_state_path, sizeof rt_state_path, "%s.s%d", state_base, slot);
            rt_state_req = touch_hit(&tbtn[TB_SAVE], x, y) ? 1 : 2;
        }
    }
    tfinger[slot_i].on = 1; tfinger[slot_i].id = e->tfinger.fingerId; tfinger[slot_i].x = e->tfinger.x; tfinger[slot_i].y = e->tfinger.y;
}

/* ---- drawing: round buttons with a gold ring, a cross with a gold edge and pill-shaped shoulder buttons, in the colours of the theme ---- */
static void tcol(uint32_t c, int a) { SDL_SetRenderDrawColor(ren, c >> 16, (c >> 8) & 255, c & 255, a); }

/* how far the row j of a rounded box (height h, corner radius r) is cut in from the left edge */
static int tinset(int j, int h, int r) {
    float dy = j < r ? r - j - 0.5f : j >= h - r ? j - (h - r) + 0.5f : 0;
    if (dy >= r) return r;
    return r - (int)sqrtf((float)r * r - dy * dy);
}

/* a rounded box (a circle when w == h, a pill when r = h/2); t > 0 draws only a ring of that thickness */
static void tround(int x, int y, int w, int h, int r, int t) {
    for (int j = 0; j < h; j++) {
        int o = tinset(j, h, r);
        if (t <= 0 || j < t || j >= h - t) {
            SDL_Rect q = {x + o, y + j, w - 2 * o, 1};
            SDL_RenderFillRect(ren, &q);
        } else {
            int in = t + tinset(j - t, h - 2 * t, r > t ? r - t : 0);
            if (in < o + t) in = o + t;
            SDL_Rect a = {x + o, y + j, in - o, 1}, b = {x + w - in, y + j, in - o, 1};
            SDL_RenderFillRect(ren, &a); SDL_RenderFillRect(ren, &b);
        }
    }
}

static void tlabel(const TouchBtn *b, int cx, int cy, int hh, int down) {
    if (!b->tex) return;
    int sc = (int)(hh * 0.42f / b->lh); if (sc < 1) sc = 1;
    int lw = b->lw * sc, lh = b->lh * sc;
    SDL_Rect d = {cx - lw / 2, cy - lh / 2, lw, lh};
    SDL_SetTextureAlphaMod(b->tex, down ? 255 : 190);
    SDL_RenderCopy(ren, b->tex, NULL, &d);
}

static void touch_draw(u32 held) {
    touch_layout();
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    int ring = (int)(tu * 0.07f); if (ring < 2) ring = 2;
    for (int i = 0; i < TB_N; i++) {
        TouchBtn *b = &tbtn[i];
        int down = 0;
        if (i == TB_DPAD) down = (held & 0xF0) != 0;
        else if (i == TB_REWIND) down = touch_rewind;
        else if (b->mask) down = (held & b->mask) != 0;
        int small = i >= TB_SAVE;                 /* the three quick buttons stay discreet */
        int fa = down ? 190 : (small ? 60 : 95);   /* body */
        int ra = down ? 255 : (small ? 120 : 190); /* ring */
        uint32_t body = down ? ui_mix(th_panel, th_gold, 1, 2) : th_bg;
        if (b->shape == 2) { /* the cross */
            float s = b->w, a = 0.62f * s;
            int cx = (int)b->cx, cy = (int)b->cy, S = (int)s, A = (int)(a / 2);
            struct { int x, y, w, h; u32 bit; } arm[5] = {
                {cx - A, cy - S, 2 * A, S - A, 64}, {cx - A, cy + A, 2 * A, S - A, 128},
                {cx - S, cy - A, S - A, 2 * A, 32}, {cx + A, cy - A, S - A, 2 * A, 16}, {cx - A, cy - A, 2 * A, 2 * A, 0}};
            for (int k = 0; k < 5; k++) {
                int on = arm[k].bit && (held & arm[k].bit);
                if (on) tcol(th_gold, 210); else tcol(th_bg, fa);
                SDL_Rect r = {arm[k].x, arm[k].y, arm[k].w, arm[k].h};
                SDL_RenderFillRect(ren, &r);
            }
            SDL_Point pts[13] = {{cx - A, cy - S}, {cx + A, cy - S}, {cx + A, cy - A}, {cx + S, cy - A}, {cx + S, cy + A}, {cx + A, cy + A},
                                 {cx + A, cy + S}, {cx - A, cy + S}, {cx - A, cy + A}, {cx - S, cy + A}, {cx - S, cy - A}, {cx - A, cy - A}, {cx - A, cy - S}};
            tcol(th_gold, ra);
            for (int k = 0; k < ring; k++) {
                SDL_Point q[13];
                for (int m = 0; m < 13; m++) { q[m] = pts[m]; q[m].x += (pts[m].x < cx ? k : -k); q[m].y += (pts[m].y < cy ? k : -k); }
                SDL_RenderDrawLines(ren, q, 13);
            }
            continue;
        }
        int x, y, w, h, r;
        if (b->shape == 0) { w = h = (int)(2 * b->w); r = w / 2; }
        else { w = (int)b->w; h = (int)b->h; r = h / 2; }
        x = (int)(b->cx - w / 2.0f); y = (int)(b->cy - h / 2.0f);
        tcol(body, fa);
        tround(x, y, w, h, r, 0);
        tcol(th_gold, ra);
        tround(x, y, w, h, r, ring);
        if (b->shape == 0) { /* a second thin ring inside, like the buttons of the console */
            int g = ring * 3;
            tcol(th_gold, ra / 3);
            tround(x + g, y + g, w - 2 * g, h - 2 * g, r - g, 1);
        }
        tlabel(b, x + w / 2, y + h / 2, b->shape == 0 ? (int)(h * 1.1f) : h, down);
    }
    SDL_RenderSetLogicalSize(ren, 240, 160);
}
