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
    b[TB_DPAD].cx = 2.3f * tu; b[TB_DPAD].cy = th - 2.3f * tu; b[TB_DPAD].w = b[TB_DPAD].h = 2.2f * tu; /* half-size of the pad */
    b[TB_A].cx = tw - 1.5f * tu; b[TB_A].cy = th - 2.7f * tu; b[TB_A].w = b[TB_A].h = 1.0f * tu; /* radius */
    b[TB_B].cx = tw - 3.2f * tu; b[TB_B].cy = th - 1.5f * tu; b[TB_B].w = b[TB_B].h = 1.0f * tu;
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
        text_draw(&c, 2, 1, s, UI_TEXT, 0);
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

static void touch_draw(u32 held) {
    touch_layout();
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < TB_N; i++) {
        TouchBtn *b = &tbtn[i];
        int down = 0;
        if (i == TB_DPAD) down = (held & 0xF0) != 0;
        else if (i == TB_REWIND) down = touch_rewind;
        else if (b->mask) down = (held & b->mask) != 0;
        int a = down ? 140 : 38;
        SDL_SetRenderDrawColor(ren, 20, 24, 40, a);
        if (b->shape == 2) {
            float s = b->w, t = 0.62f * b->w; /* a cross */
            SDL_Rect h = {(int)(b->cx - s), (int)(b->cy - t / 3), (int)(2 * s), (int)(2 * t / 3)};
            SDL_Rect v = {(int)(b->cx - t / 3), (int)(b->cy - s), (int)(2 * t / 3), (int)(2 * s)};
            SDL_RenderFillRect(ren, &h); SDL_RenderFillRect(ren, &v);
            SDL_SetRenderDrawColor(ren, 232, 197, 71, 90);
            SDL_RenderDrawRect(ren, &h); SDL_RenderDrawRect(ren, &v);
            continue;
        }
        SDL_Rect r;
        if (b->shape == 0) { float rr = b->w; r = (SDL_Rect){(int)(b->cx - rr), (int)(b->cy - rr), (int)(2 * rr), (int)(2 * rr)}; }
        else r = (SDL_Rect){(int)(b->cx - b->w / 2), (int)(b->cy - b->h / 2), (int)b->w, (int)b->h};
        SDL_RenderFillRect(ren, &r);
        SDL_SetRenderDrawColor(ren, 232, 197, 71, 90);
        SDL_RenderDrawRect(ren, &r);
        if (b->tex) {
            int sc = (int)(r.h * 0.32f / b->lh); if (sc < 1) sc = 1;
            int lw = b->lw * sc, lh = b->lh * sc;
            SDL_Rect d = {r.x + (r.w - lw) / 2, r.y + (r.h - lh) / 2, lw, lh};
            SDL_SetTextureAlphaMod(b->tex, down ? 255 : 150);
            SDL_RenderCopy(ren, b->tex, NULL, &d);
        }
    }
    SDL_RenderSetLogicalSize(ren, 240, 160);
}
