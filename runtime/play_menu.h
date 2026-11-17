/* play_menu.h: the settings and the in-game menu of play.c (F1 or Esc; Start+Back on a controller). Included by play.c
 * after its statics (window, renderer, texture, controller, audio). The look is play_ui.h. The game stops while the menu is
 * open: menu_run() draws it over the last picture and returns when it closes. Settings live in config.ini (in the saves
 * folder), are applied at once and saved when the menu closes. */
#include "play_ui.h"

typedef struct { int lang; int fit; int smooth; int fullscreen; int volume; } Settings; /* lang: index into langs[] */
static Settings cfg;
static char cfg_path[600], game_dir[512] = ".";
static uint8_t *rom_orig; /* the ROM as read from disk: the language is applied on a copy that is restored first */
static int menu_quit;

static const struct { const char *code, *name; } langs[] = { {"", "English"}, {"es", "Espa\xC3\xB1ol"} };
#define N_LANGS 2

static void cfg_defaults(void) { cfg.lang = 0; cfg.fit = 0; cfg.smooth = 0; cfg.fullscreen = 0; cfg.volume = 100; }

static int lang_index(const char *code) {
    for (int i = 0; i < N_LANGS; i++) if (!strcmp(langs[i].code, code)) return i;
    return 0;
}

static void cfg_load(void) {
    cfg_defaults();
    FILE *f = cfg_path[0] ? fopen(cfg_path, "r") : NULL;
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        char *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        if (!strcmp(line, "language")) cfg.lang = lang_index(v);
        else if (!strcmp(line, "scaling")) cfg.fit = !strcmp(v, "fit");
        else if (!strcmp(line, "smooth")) cfg.smooth = atoi(v) != 0;
        else if (!strcmp(line, "fullscreen")) cfg.fullscreen = atoi(v) != 0;
        else if (!strcmp(line, "volume")) { cfg.volume = atoi(v); if (cfg.volume < 0) cfg.volume = 0; if (cfg.volume > 100) cfg.volume = 100; }
    }
    fclose(f);
}

static void cfg_save(void) {
    FILE *f = cfg_path[0] ? fopen(cfg_path, "w") : NULL;
    if (!f) return;
    fprintf(f, "# Options of the player. Edit by hand or from the menu (F1).\nlanguage=%s\nscaling=%s\nsmooth=%d\nfullscreen=%d\nvolume=%d\n",
            langs[cfg.lang].code, cfg.fit ? "fit" : "integer", cfg.smooth, cfg.fullscreen, cfg.volume);
    fclose(f);
}

/* the window and the picture follow the settings */
static void make_texture(void) {
    if (tex) SDL_DestroyTexture(tex);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth ? "1" : "0");
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, 240, 160);
}

static void apply_video(void) {
    SDL_RenderSetIntegerScale(ren, cfg.fit ? SDL_FALSE : SDL_TRUE);
    SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    make_texture();
}

/* The language is applied by putting the ROM back as it was, patching it again and soft-resetting the game. */
static void apply_language(void) {
    memcpy(rt_rom, rom_orig, rt_rom_len);
    rt_game_init(game_dir, langs[cfg.lang].code);
    if (rt_reset_armed) longjmp(rt_reset_jb, 1);
}

/* ---- text of the menu: English, and Spanish when the game is in Spanish ---- */

static const char *tr(const char *s) {
    static const char *es[][2] = {
        {"Game", "Juego"}, {"Video", "V\xC3\xAD" "deo"}, {"Audio", "Sonido"}, {"Language", "Idioma"},
        {"Restart the game", "Reiniciar el juego"}, {"Restore defaults", "Restaurar valores"}, {"Quit", "Salir"},
        {"Scaling", "Escalado"}, {"Whole pixels", "P\xC3\xAD" "xeles enteros"}, {"Fit window", "Ajustar a la ventana"},
        {"Smoothing", "Suavizado"}, {"Full screen", "Pantalla completa"}, {"On", "S\xC3\xAD"}, {"Off", "No"},
        {"Volume", "Volumen"}, {"Changing it restarts the game", "Cambiarlo reinicia el juego"},
        {"Tab: tabs  Esc: close", "Tab: pestañas  Esc: salir"},
        {"Menu", "Menú"},
    };
    if (cfg.lang == 1) for (size_t i = 0; i < sizeof es / sizeof es[0]; i++) if (!strcmp(es[i][0], s)) return es[i][1];
    return s;
}

/* ---- the rows ---- */

enum { TAB_GAME, TAB_VIDEO, TAB_AUDIO, TAB_COUNT };
static const char *tab_names[TAB_COUNT] = {"Game", "Video", "Audio"};
static int menu_tab, menu_sel[TAB_COUNT];

static int row_count(int tab) { return tab == TAB_GAME ? 4 : tab == TAB_VIDEO ? 3 : 1; }

/* the label and the value shown of a row; kind: 0 plain action, 1 choice, 2 bar */
static void row_text(int tab, int r, const char **label, char *val, size_t n, int *kind, int *pct) {
    *kind = 1; *pct = 0; val[0] = 0;
    switch (tab * 10 + r) {
    case 0:  *label = "Language"; snprintf(val, n, "%s", langs[cfg.lang].name); break;
    case 1:  *label = "Restart the game"; *kind = 0; break;
    case 2:  *label = "Restore defaults"; *kind = 0; break;
    case 3:  *label = "Quit"; *kind = 0; break;
    case 10: *label = "Scaling"; snprintf(val, n, "%s", tr(cfg.fit ? "Fit window" : "Whole pixels")); break;
    case 11: *label = "Smoothing"; snprintf(val, n, "%s", tr(cfg.smooth ? "On" : "Off")); break;
    case 12: *label = "Full screen"; snprintf(val, n, "%s", tr(cfg.fullscreen ? "On" : "Off")); break;
    case 20: *label = "Volume"; *kind = 2; *pct = cfg.volume; snprintf(val, n, "%d%%", cfg.volume); break;
    default: *label = ""; break;
    }
}

static void menu_change(int dir) { /* dir: -1, +1, or 0 for Enter */
    int s = menu_sel[menu_tab], d = dir ? dir : 1;
    switch (menu_tab * 10 + s) {
    case 0: cfg.lang = (cfg.lang + d + N_LANGS) % N_LANGS; cfg_save(); apply_language(); break;
    case 1: if (dir == 0) { cfg_save(); apply_language(); } break;
    case 2: if (dir == 0) { int l = cfg.lang; cfg_defaults(); cfg.lang = l; apply_video(); } break;
    case 3: if (dir == 0) menu_quit = 1; break;
    case 10: cfg.fit = !cfg.fit; apply_video(); break;
    case 11: cfg.smooth = !cfg.smooth; apply_video(); break;
    case 12: cfg.fullscreen = !cfg.fullscreen; apply_video(); break;
    case 20: cfg.volume += 10 * d; if (cfg.volume < 0) cfg.volume = 0; if (cfg.volume > 100) cfg.volume = 100; break;
    }
}

static void menu_draw(Canvas *cv, const uint8_t *bg) {
    ui_backdrop(cv, bg);
    int pw = 224, ph = 144, px = (cv->w - pw) / 2, py = (cv->h - ph) / 2;
    ui_panel(cv, px, py, pw, ph);
    text_center(cv, px, pw, py + 7, tr("Menu"), UI_GOLD);
    int tx = px + 10, tw = (pw - 20) / TAB_COUNT;
    for (int t = 0; t < TAB_COUNT; t++) {
        int on = t == menu_tab;
        text_center(cv, tx + t * tw, tw, py + 19, tr(tab_names[t]), on ? UI_GOLD : UI_DIM);
        if (on) ui_rule(cv, tx + t * tw + 4, py + 29, tw - 8);
    }
    int n = row_count(menu_tab);
    for (int r = 0; r < n; r++) {
        const char *label; char val[48]; int kind, pct, y = py + 38 + r * 14;
        row_text(menu_tab, r, &label, val, sizeof val, &kind, &pct);
        int sel = r == menu_sel[menu_tab];
        if (sel) ui_cursor(cv, px + 8, y + 1);
        text_draw(cv, px + 20, y, tr(label), sel ? UI_TEXT : UI_DIM, 1);
        if (kind == 2) { ui_bar(cv, px + pw - 100, y + 1, 60, 7, pct); text_right(cv, px + pw - 12, y, val, UI_TEXT); }
        else if (kind == 1) {
            text_right(cv, px + pw - 20, y, val, sel ? UI_GOLD : UI_TEXT);
            if (sel) { ui_arrow(cv, px + pw - 20 - text_w(val) - 8, y + 1, -1, UI_GOLD); ui_arrow(cv, px + pw - 16, y + 1, 1, UI_GOLD); }
        }
    }
    if (menu_tab == TAB_GAME && menu_sel[TAB_GAME] == 0) text_center(cv, px, pw, py + ph - 24, tr("Changing it restarts the game"), UI_WARN);
    text_center(cv, px, pw, py + ph - 14, tr("Tab: tabs  Esc: close"), UI_DIM);
}

static void menu_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: menu_sel[menu_tab] = (menu_sel[menu_tab] + row_count(menu_tab) - 1) % row_count(menu_tab); break;
    case SDLK_DOWN: menu_sel[menu_tab] = (menu_sel[menu_tab] + 1) % row_count(menu_tab); break;
    case SDLK_LEFT: menu_change(-1); break;
    case SDLK_RIGHT: menu_change(1); break;
    case SDLK_RETURN: case SDLK_SPACE: menu_change(0); break;
    case SDLK_TAB: case SDLK_e: case SDLK_PAGEDOWN: menu_tab = (menu_tab + 1) % TAB_COUNT; break;
    case SDLK_q: case SDLK_PAGEUP: menu_tab = (menu_tab + TAB_COUNT - 1) % TAB_COUNT; break;
    }
}

/* Runs the menu until it closes (Esc, F1, B on a controller). Returns 1 when the user chose Quit. */
static int menu_run(void) {
    static uint8_t bg[240 * 160 * 3], out[240 * 160 * 3];
    static uint32_t px[240 * 160];
    Canvas cv = {out, 240, 160};
    for (int i = 0; i < 240 * 160; i++) {
        uint32_t c = ((uint32_t *)ppu_frame)[i];
        bg[i * 3] = (uint8_t)(c >> 16); bg[i * 3 + 1] = (uint8_t)(c >> 8); bg[i * 3 + 2] = (uint8_t)c;
    }
    if (audio) SDL_ClearQueuedAudio(audio);
    menu_quit = 0;
    for (int open = 1; open && !menu_quit;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) menu_quit = 1;
            else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_F1) open = 0;
                else if (e.key.keysym.sym == SDLK_F11) { cfg.fullscreen = !cfg.fullscreen; apply_video(); }
                else menu_key(e.key.keysym.sym);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: menu_key(SDLK_UP); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: menu_key(SDLK_DOWN); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: menu_key(SDLK_LEFT); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: menu_key(SDLK_RIGHT); break;
                case SDL_CONTROLLER_BUTTON_A: menu_key(SDLK_RETURN); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: menu_key(SDLK_q); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: menu_key(SDLK_e); break;
                case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_START: open = 0; break;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        menu_draw(&cv, bg);
        for (int i = 0; i < 240 * 160; i++) px[i] = (uint32_t)out[i * 3] << 16 | (uint32_t)out[i * 3 + 1] << 8 | out[i * 3 + 2];
        SDL_UpdateTexture(tex, NULL, px, 240 * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    cfg_save();
    next_tick = 0; /* do not run to catch up with the time the menu took */
    return menu_quit;
}

/* --menu-shot FILE.ppm [--menu-tab N] [--menu-row N]: writes the menu drawn over the picture of frame 120 and quits (for tests and screenshots). */
static const char *menu_shot_path;
static void menu_shot(void) {
    static uint8_t bg[240 * 160 * 3], out[240 * 160 * 3];
    Canvas cv = {out, 240, 160};
    for (int i = 0; i < 240 * 160; i++) {
        uint32_t c = ((uint32_t *)ppu_frame)[i];
        bg[i * 3] = (uint8_t)(c >> 16); bg[i * 3 + 1] = (uint8_t)(c >> 8); bg[i * 3 + 2] = (uint8_t)c;
    }
    menu_draw(&cv, bg);
    FILE *f = fopen(menu_shot_path, "wb");
    if (f) { fprintf(f, "P6
240 160
255
"); fwrite(out, 1, sizeof out, f); fclose(f); }
    exit(0);
}
