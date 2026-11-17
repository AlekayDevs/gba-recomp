/* play_menu.h: the settings and the in-game menu of play.c (F1 or Esc; Start+Back on a controller). Included by play.c
 * after its statics (window, renderer, texture, controller, audio). The pieces of the look are in play_ui.h.
 *
 * The menu is a drawer on the left of the window: the game stays visible and paused on the right, drawn with the
 * scaling and smoothing in use, so a change in Video shows at once. The drawer has a sidebar of categories, the rows of
 * the chosen category, a line that says what the chosen row does and the keys. It is laid out for a canvas about 480 by
 * 270 "pixels" of the interface, each one `k` screen pixels wide (k from the window size), so it is as readable in a small
 * window as in full screen. The whole window is composed in software into one texture, only when something changed.
 * The colours and the name come from the game's profile (--theme, --brand) and the emblem from <game dir>/emblem.bmp.
 * Settings live in config.ini (in the saves folder), apply at once and are saved when the menu closes. */
#include "play_ui.h"

typedef struct { int lang; int fit; int smooth; int fullscreen; int volume; int rewind; int borders; int t_opacity, t_size, t_quick, t_haptic, t_left, t_hide;
    float tb_dx[11], tb_dy[11]; int tb_sc[11], tb_op[11]; /* per touch button: offset (fraction of the window), size % and opacity % */ } Settings;
static int touch_edit_req; /* the menu asks play.c to open the editor of the touch buttons */
static void touch_layout_reset(void); /* lang: index into langs[] */
static Settings cfg;
/* The controls: for each button of the GBA (bit i of KEYINPUT: A B Select Start Right Left Up Down R L) a keyboard
 * scancode and a controller button, one of each. Kept in config.ini as key_<name> (scancode name) and pad_<name> (number). */
static const char *gba_btn[10] = {"a", "b", "select", "start", "right", "left", "up", "down", "r", "l"};
static int kb[10], pb[10];
static void bind_defaults(void) {
    static const int k[10] = {SDL_SCANCODE_X, SDL_SCANCODE_Z, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT,
                              SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_S, SDL_SCANCODE_A};
    static const int b[10] = {SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
                              SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP,
                              SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER};
    for (int i = 0; i < 10; i++) { kb[i] = k[i]; pb[i] = b[i]; }
}
static char cfg_path[600], game_dir[512] = ".", brand[64] = "";
static uint8_t *rom_orig; /* the ROM as read from disk: the language is applied on a copy that is restored first */
static int menu_quit;

/* the look: night blue unless the profile says otherwise */
static uint32_t th_bg = 0x0C1440, th_panel = 0x1C2C78, th_gold = 0xF8D860, th_ink = 0xF4F0E0;
static uint8_t emblem[32 * 32 * 3];
static int emblem_ok;

static const struct { const char *code, *name; } langs[] = { {"", "English"}, {"es", "Espa\xC3\xB1ol"} };
#define N_LANGS 2

static void cfg_defaults(void) { cfg.lang = 0; cfg.fit = 0; cfg.smooth = 0; cfg.fullscreen = 0; cfg.volume = 100; cfg.rewind = 1; cfg.borders = 1; cfg.t_opacity = 100; cfg.t_size = 100; cfg.t_quick = 1; cfg.t_haptic = 1; cfg.t_left = 0; cfg.t_hide = 1; for (int i = 0; i < 11; i++) { cfg.tb_dx[i] = cfg.tb_dy[i] = 0; cfg.tb_sc[i] = cfg.tb_op[i] = 100; } }

static int lang_index(const char *code) {
    for (int i = 0; i < N_LANGS; i++) if (!strcmp(langs[i].code, code)) return i;
    return 0;
}

static void cfg_load(void) {
    cfg_defaults();
    bind_defaults();
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
        else if (!strcmp(line, "borders")) cfg.borders = atoi(v) != 0;
        else if (!strcmp(line, "fullscreen")) cfg.fullscreen = atoi(v) != 0;
        else if (!strcmp(line, "touch_opacity")) { cfg.t_opacity = atoi(v); if (cfg.t_opacity < 20) cfg.t_opacity = 20; if (cfg.t_opacity > 100) cfg.t_opacity = 100; }
        else if (!strcmp(line, "touch_size")) { cfg.t_size = atoi(v); if (cfg.t_size < 70) cfg.t_size = 70; if (cfg.t_size > 130) cfg.t_size = 130; }
        else if (!strcmp(line, "touch_quick")) cfg.t_quick = atoi(v) != 0;
        else if (!strcmp(line, "touch_haptic")) cfg.t_haptic = atoi(v) != 0;
        else if (!strcmp(line, "touch_left")) cfg.t_left = atoi(v) != 0;
        else if (!strcmp(line, "touch_hide")) cfg.t_hide = atoi(v) != 0;
        else if (!strncmp(line, "tb_", 3) && line[3] >= '0' && line[3] <= '9') {
            int i = atoi(line + 3), sc, op; float dx, dy;
            if (i >= 0 && i < 11 && sscanf(v, "%f,%f,%d,%d", &dx, &dy, &sc, &op) == 4) {
                cfg.tb_dx[i] = dx; cfg.tb_dy[i] = dy;
                cfg.tb_sc[i] = sc < 50 ? 50 : sc > 200 ? 200 : sc; cfg.tb_op[i] = op < 20 ? 20 : op > 100 ? 100 : op;
            }
        }
        else if (!strncmp(line, "key_", 4) || !strncmp(line, "pad_", 4)) {
            for (int i = 0; i < 10; i++) if (!strcmp(line + 4, gba_btn[i])) {
                if (line[0] == 'k') { SDL_Scancode sc = SDL_GetScancodeFromName(v); if (sc != SDL_SCANCODE_UNKNOWN) kb[i] = sc; }
                else if (atoi(v) >= 0 && atoi(v) < SDL_CONTROLLER_BUTTON_MAX) pb[i] = atoi(v);
            }
        }
        else if (!strcmp(line, "rewind")) cfg.rewind = atoi(v) != 0;
        else if (!strncmp(line, "opt_", 4)) rt_opt_set_id(line + 4, atoi(v));
        else if (!strcmp(line, "volume")) { cfg.volume = atoi(v); if (cfg.volume < 0) cfg.volume = 0; if (cfg.volume > 100) cfg.volume = 100; }
    }
    fclose(f);
}

static void cfg_save(void) {
    FILE *f = cfg_path[0] ? fopen(cfg_path, "w") : NULL;
    if (!f) return;
    fprintf(f, "# Options of the player. Edit by hand or from the menu (F1).\nlanguage=%s\nscaling=%s\nsmooth=%d\nfullscreen=%d\nvolume=%d\nrewind=%d\nborders=%d\n",
            langs[cfg.lang].code, cfg.fit ? "fit" : "integer", cfg.smooth, cfg.fullscreen, cfg.volume, cfg.rewind, cfg.borders);
    fprintf(f, "touch_opacity=%d\ntouch_size=%d\ntouch_quick=%d\ntouch_haptic=%d\ntouch_left=%d\ntouch_hide=%d\n", cfg.t_opacity, cfg.t_size, cfg.t_quick, cfg.t_haptic, cfg.t_left, cfg.t_hide);
    for (int i = 0; i < 11; i++) fprintf(f, "tb_%d=%.4f,%.4f,%d,%d\n", i, cfg.tb_dx[i], cfg.tb_dy[i], cfg.tb_sc[i], cfg.tb_op[i]);
    for (int i = 0; i < rt_opt_count(); i++) fprintf(f, "opt_%s=%d\n", rt_opt_get(i)->id, rt_opt_val[i]);
    for (int i = 0; i < 10; i++) fprintf(f, "key_%s=%s\npad_%s=%d\n", gba_btn[i], SDL_GetScancodeName((SDL_Scancode)kb[i]), gba_btn[i], pb[i]);
    fclose(f);
}

/* --theme BG,PANEL,GOLD,INK as RRGGBB */
static void theme_parse(const char *s) {
    uint32_t v[4]; int n = 0;
    while (n < 4 && *s) { v[n++] = (uint32_t)strtoul(s, NULL, 16); while (*s && *s != ',') s++; if (*s == ',') s++; }
    if (n == 4) { th_bg = v[0]; th_panel = v[1]; th_gold = v[2]; th_ink = v[3]; }
}

/* <game dir>/emblem.bmp: 32 by 32, 24 bits, magenta is transparent (drawn by the game's repository, not from the game) */
static void emblem_load(void) {
    char path[640];
    snprintf(path, sizeof path, "%s/emblem.bmp", game_dir);
    SDL_Surface *s = SDL_LoadBMP(path);
    if (!s) return;
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(s);
    if (!c) return;
    if (c->w == 32 && c->h == 32) {
        for (int y = 0; y < 32; y++) memcpy(emblem + y * 96, (uint8_t *)c->pixels + y * c->pitch, 96);
        emblem_ok = 1;
    }
    SDL_FreeSurface(c);
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

/* The side bars: when the window is wider than the picture, the free space gets stripes in the colours of the theme and a thin
   frame around the picture. Drawn in window pixels (no logical size), before the picture itself. */
static void draw_bars(void) {
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    double f = (double)ww / 240 < (double)wh / 160 ? (double)ww / 240 : (double)wh / 160;
    if (!cfg.fit && f >= 1) f = (int)f;
    int dw = (int)(240 * f), dh = (int)(160 * f), x0 = (ww - dw) / 2, y0 = (wh - dh) / 2;
    if (x0 < 4 && y0 < 4) return;
    SDL_RenderSetLogicalSize(ren, 0, 0);
    SDL_SetRenderDrawColor(ren, th_bg >> 16, (th_bg >> 8) & 255, th_bg & 255, 255);
    SDL_RenderClear(ren);
    int step = (int)(24 * (f < 1 ? 1 : f) / 2) * 2 + 8;
    SDL_SetRenderDrawColor(ren, th_panel >> 16, (th_panel >> 8) & 255, th_panel & 255, 255);
    for (int x = x0 % step - step; x < ww; x += step) {
        SDL_Rect r = {x, 0, step / 3, wh};
        SDL_RenderFillRect(ren, &r);
    }
    SDL_SetRenderDrawColor(ren, th_gold >> 16, (th_gold >> 8) & 255, th_gold & 255, 255);
    int t = f >= 3 ? 3 : 2;
    SDL_Rect fr[2] = {{x0 - t, y0 - t, dw + 2 * t, dh + 2 * t}, {x0 - t - 4, y0 - t - 4, dw + 2 * t + 8, dh + 2 * t + 8}};
    SDL_RenderDrawRect(ren, &fr[0]);
    SDL_RenderDrawRect(ren, &fr[1]);
    SDL_Rect pic = {x0, y0, dw, dh};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderFillRect(ren, &pic);
    SDL_RenderSetLogicalSize(ren, 240, 160);
}

/* The language is applied by putting the ROM back as it was, patching it again and soft-resetting the game. */
static void apply_language(void) {
    rt_rewind_clear();
    memcpy(rt_rom, rom_orig, rt_rom_len);
    rt_game_init(game_dir, langs[cfg.lang].code);
    if (rt_reset_armed) longjmp(rt_reset_jb, 1);
}

/* ---- the text of the menu: English, and Spanish when the game is in Spanish ---- */

static const char *tr(const char *s) {
    static const char *es[][2] = {
        {"Settings", "Ajustes"}, {"Game", "Juego"}, {"Video", "V\xC3\xAD" "deo"}, {"Audio", "Sonido"},
        {"Language", "Idioma"}, {"Restart the game", "Reiniciar el juego"}, {"Restore defaults", "Restaurar valores"}, {"Quit", "Salir"},
        {"Scaling", "Escalado"}, {"Whole", "Enteros"}, {"Fit", "Ajustado"},
        {"Smoothing", "Suavizado"}, {"Full screen", "Pantalla completa"}, {"Side bars", "Barras laterales"}, {"On", "S\xC3\xAD"}, {"Off", "No"}, {"Volume", "Volumen"},
        {"Language of the game's text. Changing it restarts the game; your saved game is kept.",
         "Idioma del texto del juego. Cambiarlo reinicia el juego; la partida guardada se conserva."},
        {"Starts the game again from the title screen. The saved game is kept.", "Vuelve a empezar desde la pantalla de t\xC3\xADtulo. La partida guardada se conserva."},
        {"Puts video and audio back to their defaults. The language stays.", "Devuelve el v\xC3\xAD" "deo y el sonido a sus valores. El idioma no cambia."},
        {"Closes the game.", "Cierra el juego."},
        {"Whole keeps every pixel the same size and the picture sharp; Fit fills the window, with uneven pixels.",
         "Enteros mantiene los p\xC3\xADxeles iguales y la imagen n\xC3\xADtida; Ajustado llena la ventana, con p\xC3\xADxeles desiguales."},
        {"Softens the edges of the scaled picture.", "Suaviza los bordes de la imagen ampliada."},
        {"Switches between a window and the whole screen (F11).", "Alterna entre ventana y pantalla completa (F11)."},
        {"Fills the space at the sides of the picture, on a wide window, with a pattern in the colours of the game.", "Llena el espacio a los lados de la imagen, en una ventana ancha, con un dibujo en los colores del juego."},
        {"Volume of the game's sound.", "Volumen del sonido del juego."},
        {"Opacity", "Opacidad"}, {"Size", "Tama\xC3\xB1o"}, {"Quick buttons", "Botones r\xC3\xA1pidos"},
        {"Vibration", "Vibraci\xC3\xB3n"}, {"Left-handed", "Modo zurdo"}, {"Hide with controller", "Ocultar con mando"},
        {"Edit buttons", "Editar botones"}, {"Reset", "Restablecer"}, {"Done", "Hecho"}, {"Drag a button", "Arrastra un bot\xC3\xB3n"},
        {"Drag each button where you want it and set its size and opacity.", "Arrastra cada bot\xC3\xB3n adonde quieras y ajusta su tama\xC3\xB1o y opacidad."},
        {"A short tick when you press a button on the screen.", "Una vibraci\xC3\xB3n corta al pulsar un bot\xC3\xB3n de la pantalla."},
        {"Puts the pad on the right and A and B on the left.", "Pone la cruceta a la derecha y A y B a la izquierda."},
        {"Hides the buttons on the screen while a controller is connected (touching the screen brings them back).", "Oculta los botones de la pantalla mientras hay un mando conectado (al tocar la pantalla vuelven)."},
        {"How solid the buttons on the screen look while you are not touching them.", "Lo opacos que se ven los botones de la pantalla cuando no los tocas."},
        {"Size of the buttons on the screen.", "Tama\xC3\xB1o de los botones de la pantalla."},
        {"Shows SAVE, LOAD and REWIND at the top of the screen. MENU always stays.", "Muestra SAVE, LOAD y REWIND arriba. MENU siempre se queda."},
        {"Puts the buttons back to their place, size and opacity.", "Devuelve los botones a su sitio, tama\xC3\xB1o y opacidad."},
        {"Tap to choose   Tap outside to close", "Toca para elegir   Toca fuera para cerrar"},
        {"Controls", "Controles"}, {"Extras", "Extras"}, {"States", "Estados"}, {"Slot", "Ranura"}, {"Save state", "Guardar estado"}, {"Load state", "Cargar estado"}, {"Rewind", "Rebobinar"},
        {"Hold R (L3 on a controller) to go back in time, up to a minute. Keeping the history costs a little processor time.", "Mant\xC3\xA9n R (L3 en el mando) para volver atr\xC3\xA1s en el tiempo, hasta un minuto. Guardar el historial gasta un poco de procesador."},
        {"Which of the 9 slots Save and Load use (the keys 1 to 9 choose it too).", "Qu\xC3\xA9 ranura de las 9 usan Guardar y Cargar (las teclas 1 a 9 tambi\xC3\xA9n la eligen)."},
        {"Saves the game as it is (F5). It is written at the next step of the game\x27s main loop.", "Guarda el juego tal como est\xC3\xA1 (F5). Se escribe en el siguiente paso del bucle principal."},
        {"Goes back to the state saved in this slot (F8). Your saved game in the cartridge is not touched.", "Vuelve al estado guardado en esta ranura (F8). La partida del cartucho no se toca."}, {"Up", "Arriba"}, {"Down", "Abajo"}, {"Left", "Izquierda"}, {"Right", "Derecha"}, {"Restore controls", "Restaurar controles"},
        {"Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels.",
         "Elige la tecla y el bot\xC3\xB3n del mando de este. Intro y luego pulsa lo que quieras; Esc cancela."},
        {"Puts every key and button back to its default.", "Devuelve todas las teclas y botones a su valor."},
        {"Press a key or a controller button. Esc cancels.", "Pulsa una tecla o un bot\xC3\xB3n del mando. Esc cancela."},
        {"Up Down: choose   Enter: change   Esc: back", "Intro: cambiar   Esc: volver"},
    };
    if (cfg.lang == 1) for (size_t i = 0; i < sizeof es / sizeof es[0]; i++) if (!strcmp(es[i][0], s)) return es[i][1];
    return s;
}

/* ---- the categories and their rows ---- */

typedef struct { const char *label, *desc; } Row;
typedef struct { const char *name; const Row *rows; int n; } Category;

static const Row rows_game[] = {
    {"Language", "Language of the game's text. Changing it restarts the game; your saved game is kept."},
    {"Restart the game", "Starts the game again from the title screen. The saved game is kept."},
    {"Restore defaults", "Puts video and audio back to their defaults. The language stays."},
    {"Quit", "Closes the game."},
};
static const Row rows_video[] = {
    {"Scaling", "Whole keeps every pixel the same size and the picture sharp; Fit fills the window, with uneven pixels."},
    {"Smoothing", "Softens the edges of the scaled picture."},
    {"Full screen", "Switches between a window and the whole screen (F11)."},
    {"Side bars", "Fills the space at the sides of the picture, on a wide window, with a pattern in the colours of the game."},
};
static const Row rows_audio[] = { {"Volume", "Volume of the game's sound."} };
/* the buttons in the order of the menu: the index into kb[] and pb[] of row r */
static const int ctl_order[10] = {6, 7, 5, 4, 0, 1, 9, 8, 3, 2};
static const Row rows_ctl[] = {
    {"Up", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Down", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Left", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Right", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"A", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"B", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"L", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"R", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Start", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Select", "Choose the key and the controller button for this one. Enter, then press what you want; Esc cancels."},
    {"Restore controls", "Puts every key and button back to its default."},
};
static const Row rows_touch[] = {
    {"Opacity", "How solid the buttons on the screen look while you are not touching them."},
    {"Size", "Size of the buttons on the screen."},
    {"Quick buttons", "Shows SAVE, LOAD and REWIND at the top of the screen. MENU always stays."},
    {"Vibration", "A short tick when you press a button on the screen."},
    {"Left-handed", "Puts the pad on the right and A and B on the left."},
    {"Hide with controller", "Hides the buttons on the screen while a controller is connected (touching the screen brings them back)."},
    {"Edit buttons", "Drag each button where you want it and set its size and opacity."},
    {"Restore controls", "Puts the buttons back to their place, size and opacity."},
};
static const Row rows_video_touch[] = {
    {"Scaling", "Whole keeps every pixel the same size and the picture sharp; Fit fills the window, with uneven pixels."},
    {"Smoothing", "Softens the edges of the scaled picture."},
    {"Side bars", "Fills the space at the sides of the picture, on a wide window, with a pattern in the colours of the game."},
};
static const Row rows_state[] = {
    {"Slot", "Which of the 9 slots Save and Load use (the keys 1 to 9 choose it too)."},
    {"Save state", "Saves the game as it is (F5). It is written at the next step of the game's main loop."},
    {"Load state", "Goes back to the state saved in this slot (F8). Your saved game in the cartridge is not touched."},
    {"Rewind", "Hold R (L3 on a controller) to go back in time, up to a minute. Keeping the history costs a little processor time."},
};
#define TAB_OPTS 5 /* the options of the game: its rows come from game_opts[] */
static const Category cats[] = {
#ifdef __ANDROID__
    {"Game", rows_game, 4}, {"Video", rows_video_touch, 3}, {"Audio", rows_audio, 1}, {"Controls", rows_touch, 8}, {"States", rows_state, 4},
#else
    {"Game", rows_game, 4}, {"Video", rows_video, 4}, {"Audio", rows_audio, 1}, {"Controls", rows_ctl, 11}, {"States", rows_state, 4},
#endif
    {"Extras", NULL, 0},
};
#define N_CATS_MAX 6
static int n_cats = 5; /* the sixth, Extras, only when the game has options */
#ifdef __ANDROID__
#define MROW 28 /* taller rows and tabs: they are tapped with a thumb */
#else
#define MROW 20
#endif
static int menu_tab;
static void menu_init(void) { if (rt_opt_count() > 0) n_cats = 6; menu_tab %= n_cats; }
static const char *opt_text(int i, int desc) {
    const GameOpt *o = rt_opt_get(i);
    if (!o) return "";
    if (cfg.lang == 1) { const char *es = desc ? o->desc_es : o->name_es; if (es && es[0]) return es; }
    return desc ? o->desc : o->name;
}
static int cat_rows(int t) { return t == TAB_OPTS ? rt_opt_count() : cats[t].n; }
static int menu_close_req; /* an action that closes the menu (saving or loading a state) */
static int menu_capture = -1; /* the row waiting for a key or a button, or -1 */

static int menu_free_scroll; /* the rows were dragged: do not force the chosen row into view */
static int menu_focus /* 0 sidebar, 1 rows */, menu_sel[N_CATS_MAX], menu_off[N_CATS_MAX], menu_dirty = 1;

static const char *pad_name(int b) {
    static const char *n[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB", "D\xE2\x86\x91", "D\xE2\x86\x93", "D\xE2\x86\x90", "D\xE2\x86\x92"};
    return b >= 0 && b < 15 ? n[b] : "?";
}

/* The value shown of a row; kind: 0 plain action, 1 choice, 2 bar, 3 a binding. */
#ifdef __ANDROID__
#define VROW(tab, r) ((tab) == 1 && (r) >= 2 ? (r) + 1 : (r)) /* no "Full screen" row on a phone */
#else
#define VROW(tab, r) (r)
#endif
static void row_value0(int tab, int r, char *val, size_t n, int *kind, int *pct) {
    *kind = 1; *pct = 0; val[0] = 0;
    switch (tab * 100 + VROW(tab, r)) {
#ifdef __ANDROID__
    case 300: *kind = 2; *pct = (cfg.t_opacity - 20) * 100 / 80; snprintf(val, n, "%d%%", cfg.t_opacity); break;
    case 301: snprintf(val, n, "%d%%", cfg.t_size); break;
    case 302: snprintf(val, n, "%s", tr(cfg.t_quick ? "On" : "Off")); break;
    case 303: case 304: case 305: snprintf(val, n, "%s", tr((VROW(tab, r) == 3 ? cfg.t_haptic : VROW(tab, r) == 4 ? cfg.t_left : cfg.t_hide) ? "On" : "Off")); break;
    case 306: case 307: *kind = 0; break;
#endif
    case 0:  snprintf(val, n, "%s", langs[cfg.lang].name); break;
    case 1: case 2: case 3: *kind = 0; break;
    case 100: snprintf(val, n, "%s", tr(cfg.fit ? "Fit" : "Whole")); break;
    case 101: snprintf(val, n, "%s", tr(cfg.smooth ? "On" : "Off")); break;
    case 102: snprintf(val, n, "%s", tr(cfg.fullscreen ? "On" : "Off")); break;
    case 103: snprintf(val, n, "%s", tr(cfg.borders ? "On" : "Off")); break;
    case 200: *kind = 2; *pct = cfg.volume; snprintf(val, n, "%d%%", cfg.volume); break;
    case 310: *kind = 0; break;
    case 400: snprintf(val, n, "%d", slot); break;
    case 401: case 402: *kind = 0; break;
    case 403: snprintf(val, n, "%s", tr(cfg.rewind ? "On" : "Off")); break;
    default:
        if (tab == TAB_OPTS) {
            const GameOpt *o = rt_opt_get(r);
            const char *names = o ? (cfg.lang == 1 && o->values_es && o->values_es[0] ? o->values_es : o->values) : NULL;
            if (o && names && names[0]) { /* the name of the chosen value: the (value - min)th of the list */
                const char *p = names; int k = rt_opt_val[r] - o->min;
                while (k-- > 0 && (p = strchr(p, ','))) p++;
                size_t len = p ? strcspn(p, ",") : 0;
                snprintf(val, n, "%.*s", (int)len, p ? p : "");
            }
            else if (o && o->min == 0 && o->max == 1) snprintf(val, n, "%s", tr(rt_opt_val[r] ? "On" : "Off"));
            else if (o) snprintf(val, n, "%d", rt_opt_val[r]);
        } else if (tab == 3 && r < 10) {
            *kind = 3;
            if (menu_capture == r) snprintf(val, n, "...");
            else snprintf(val, n, "%s · %s", SDL_GetScancodeName((SDL_Scancode)kb[ctl_order[r]]), pad_name(pb[ctl_order[r]]));
        }
        break;
    }
}

static void row_value(int tab, int r, char *val, size_t n, int *kind, int *pct) {
    row_value0(tab, r, val, n, kind, pct);
    if (*kind == 1 && (!strcmp(val, tr("On")) || !strcmp(val, tr("Off")))) *kind = 4; /* an On/Off row: drawn and tapped as a switch */
}

static void menu_change(int dir) { /* dir: -1, +1, or 0 for Enter */
    int s = menu_sel[menu_tab], d = dir ? dir : 1;
    switch (menu_tab * 100 + VROW(menu_tab, s)) {
#ifdef __ANDROID__
    case 300: cfg.t_opacity += 10 * d; if (cfg.t_opacity < 20) cfg.t_opacity = 20; if (cfg.t_opacity > 100) cfg.t_opacity = 100; break;
    case 301: cfg.t_size += 10 * d; if (cfg.t_size < 70) cfg.t_size = 70; if (cfg.t_size > 130) cfg.t_size = 130; break;
    case 302: cfg.t_quick = !cfg.t_quick; break;
    case 303: cfg.t_haptic = !cfg.t_haptic; break;
    case 304: cfg.t_left = !cfg.t_left; break;
    case 305: cfg.t_hide = !cfg.t_hide; break;
    case 306: if (dir == 0) { touch_edit_req = 1; menu_close_req = 1; } break;
    case 307: if (dir == 0) { cfg.t_opacity = 100; cfg.t_size = 100; cfg.t_quick = 1; cfg.t_haptic = 1; cfg.t_left = 0; cfg.t_hide = 1; touch_layout_reset(); } break;
#endif
    case 0: cfg.lang = (cfg.lang + d + N_LANGS) % N_LANGS; cfg_save(); apply_language(); break;
    case 1: if (dir == 0) { cfg_save(); apply_language(); } break;
    case 2: if (dir == 0) { int l = cfg.lang; cfg_defaults(); cfg.lang = l; apply_video(); } break;
    case 3: if (dir == 0) menu_quit = 1; break;
    case 100: cfg.fit = !cfg.fit; break;
    case 101: cfg.smooth = !cfg.smooth; break;
    case 102: cfg.fullscreen = !cfg.fullscreen; apply_video(); break;
    case 103: cfg.borders = !cfg.borders; break;
    case 200: cfg.volume += 10 * d; if (cfg.volume < 0) cfg.volume = 0; if (cfg.volume > 100) cfg.volume = 100; break;
    case 310: if (dir == 0) bind_defaults(); break;
    case 400: slot = (slot - 1 + d + 9) % 9 + 1; break;
    case 403: cfg.rewind = !cfg.rewind; if (!cfg.rewind) rt_rewind_clear(); break;
    case 401: case 402:
        if (dir == 0 && !rt_state_req) {
            snprintf(rt_state_path, sizeof rt_state_path, "%s.s%d", state_base, slot);
            rt_state_req = s == 1 ? 1 : 2;
            menu_close_req = 1;
        }
        break;
    default:
        if (menu_tab == TAB_OPTS) { const GameOpt *o = rt_opt_get(s); if (o) rt_opt_set(s, o->max == 1 && o->min == 0 ? !rt_opt_val[s] : rt_opt_val[s] + d); }
        else if (menu_tab == 3 && s < 10 && dir == 0) menu_capture = s;
        break;
    }
    menu_dirty = 1;
}

/* Gives the button of row r the key or controller button just pressed; what had it takes the old one (a swap). */
static void bind_set(int r, int is_pad, int code) {
    int *v = is_pad ? pb : kb, me = ctl_order[r];
    for (int i = 0; i < 10; i++) if (i != me && v[i] == code) v[i] = v[me];
    v[me] = code;
    menu_capture = -1;
    menu_dirty = 1;
}

/* ---- drawing ---- */

/* `s` broken into lines of at most `maxw` pixels at the spaces; draws up to `maxlines` of them, returns how many. */
static int text_wrap(Canvas *c, int x, int y, const char *s, int maxw, int maxlines, uint32_t rgb) {
    char line[200]; int lines = 0;
    while (*s && lines < maxlines) {
        size_t len = 0, cut = 0;
        for (;;) {
            size_t next = len;
            while (s[next] && s[next] != ' ') next++;
            if (next >= sizeof line) break;
            memcpy(line, s, next); line[next] = 0;
            if (text_w(line) > maxw && cut) break;
            cut = next; len = next;
            if (!s[next]) break;
            len = next + 1;
        }
        if (!cut) cut = strlen(s) < sizeof line - 1 ? strlen(s) : sizeof line - 1;
        memcpy(line, s, cut); line[cut] = 0;
        text_draw(c, x, y + lines * UI_LINE, line, rgb, 1);
        lines++;
        s += cut; while (*s == ' ') s++;
    }
    return lines;
}

static void emblem_draw(Canvas *c, int x, int y) {
    if (!emblem_ok) return;
    for (int j = 0; j < 32; j++) for (int i = 0; i < 32; i++) {
        const uint8_t *p = emblem + (j * 32 + i) * 3;
        if (p[0] == 255 && p[1] == 0 && p[2] == 255) continue;
        cv_put(c, x + i, y + j, (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]);
    }
}

static void ui_slider(Canvas *c, int x, int y, int w, int pct) {
    cv_rect(c, x, y, w, 7, th_gold);
    cv_rect(c, x + 1, y + 1, w - 2, 5, th_bg);
    cv_rect(c, x + 1, y + 1, (w - 2) * pct / 100, 5, ui_mix(th_gold, 0xFFFFFF, 1, 4));
}

/* The drawer into `c` (dw by the window's height in interface pixels). */
static void drawer_draw(Canvas *c) {
    int w = c->w, h = c->h;
    uint32_t dim = ui_mix(th_ink, th_bg, 1, 3);
    for (int j = 0; j < h; j++) cv_rect(c, 0, j, w, 1, ui_mix(th_panel, th_bg, j, h > 1 ? h - 1 : 1));
    cv_rect(c, w - 2, 0, 2, h, th_gold);
    cv_rect(c, w - 3, 0, 1, h, ui_mix(th_gold, th_bg, 1, 2));
    /* the header: emblem, name of the game and "Menu" */
    emblem_draw(c, 10, 6);
    text_draw(c, emblem_ok ? 50 : 12, 10, brand[0] ? brand : "gba-recomp", th_gold, 1);
    text_draw(c, emblem_ok ? 50 : 12, 24, tr("Settings"), dim, 1);
    ui_rule(c, 8, 44, w - 18);
    /* the sidebar */
    int sw = 84, top = 54;
    for (int t = 0; t < n_cats; t++) {
        int y = top + t * (MROW + 2), on = t == menu_tab;
        if (on) {
            cv_rect(c, 6, y, sw - 4, MROW - 2, ui_mix(th_panel, th_gold, menu_focus ? 1 : 2, 6));
            cv_rect(c, 6, y, 3, MROW - 2, th_gold);
        }
        text_draw(c, 16, y + MROW / 2 - 4, tr(cats[t].name), on ? th_gold : th_ink, 1);
    }
    cv_rect(c, sw + 6, top, 1, h - top - 62, ui_mix(th_gold, th_bg, 3, 4));
    /* the rows of the chosen category, scrolled so the chosen one shows */
    const Category *cat = &cats[menu_tab];
    int px = sw + 16, pw = w - px - 12, rh = MROW, vis = (h - top - 62) / rh;
    if (vis < 1) vis = 1;
    int *off = &menu_off[menu_tab], sel = menu_sel[menu_tab];
    int nrows = cat_rows(menu_tab);
    if (menu_free_scroll) { if (*off > nrows - vis) *off = nrows - vis; if (*off < 0) *off = 0; }
    else {
        if (sel < *off) *off = sel;
        if (sel >= *off + vis) *off = sel - vis + 1;
    }
    for (int r = *off; r < nrows && r < *off + vis; r++) {
        int y = top + (r - *off) * rh, on = menu_focus && r == sel;
        char val[48]; int kind, pct;
        row_value(menu_tab, r, val, sizeof val, &kind, &pct);
        if (on) { cv_rect(c, px - 6, y, pw + 8, rh - 2, ui_mix(th_panel, th_gold, 1, 6)); ui_cursor(c, px - 4, y + rh / 2 - 5); }
        else if (r == sel) cv_rect(c, px - 6, y, 2, rh - 2, ui_mix(th_gold, th_bg, 1, 2));
        text_draw(c, px + 8, y + rh / 2 - 4, menu_tab == TAB_OPTS ? opt_text(r, 0) : tr(cat->rows[r].label), on ? th_ink : dim, 1);
        if (kind == 2) {
            ui_slider(c, px + pw - 8 - text_w(val) - 52, y + rh / 2 - 3, 46, pct);
            text_right(c, px + pw - 2, y + rh / 2 - 4, val, on ? th_gold : th_ink);
        } else if (kind == 3) {
            text_right(c, px + pw - 4, y + rh / 2 - 4, val, menu_capture == r ? th_gold : on ? th_gold : th_ink);
        } else if (kind == 1) {
            text_right(c, px + pw - 12, y + rh / 2 - 4, val, on ? th_gold : th_ink);
            if (on) { ui_arrow(c, px + pw - 12 - text_w(val) - 8, y + rh / 2 - 3, -1, th_gold); ui_arrow(c, px + pw - 9, y + rh / 2 - 3, 1, th_gold); }
        } else if (kind == 4) { /* a switch */
            int onv = !strcmp(val, tr("On")), sx = px + pw - 30, sy = y + rh / 2 - 6;
            cv_rect(c, sx, sy, 26, 12, onv ? th_gold : dim);
            cv_rect(c, sx + 1, sy + 1, 24, 10, onv ? ui_mix(th_gold, th_bg, 1, 3) : th_bg);
            cv_rect(c, onv ? sx + 15 : sx + 3, sy + 3, 8, 6, onv ? th_ink : dim);
        }
    }
    if (nrows > vis) { /* there are more rows than fit */
        if (*off > 0) ui_arrow(c, w - 12, top - 2, -1, th_gold);
        if (*off + vis < nrows) ui_arrow(c, w - 12, top + vis * rh - 6, 1, th_gold);
    }
    /* what the chosen row does, and the keys */
    ui_rule(c, 8, h - 56, w - 18);
    text_wrap(c, 12, h - 48, menu_capture >= 0 ? tr("Press a key or a controller button. Esc cancels.") : menu_tab == TAB_OPTS ? opt_text(sel, 1) : tr(cat->rows[sel].desc), w - 26, 3, menu_capture >= 0 ? th_gold : dim);
#ifdef __ANDROID__
    text_draw(c, 12, h - 14, tr("Tap to choose   Tap outside to close"), ui_mix(th_ink, th_bg, 1, 2), 1);
    { /* the close button: a gold box with a cross, in the corner of the header */
        int bx = w - 36, by = 8;
        cv_rect(c, bx, by, 26, 26, th_gold);
        cv_rect(c, bx + 2, by + 2, 22, 22, th_bg);
        for (int i = 0; i < 12; i++) { cv_rect(c, bx + 7 + i, by + 7 + i, 2, 2, th_gold); cv_rect(c, bx + 18 - i, by + 7 + i, 2, 2, th_gold); }
    }
#else
    text_draw(c, 12, h - 14, tr("Up Down: choose   Enter: change   Esc: back"), ui_mix(th_ink, th_bg, 1, 2), 1);
#endif
}

/* The picture of the game in the area right of the drawer, with the scaling and smoothing of the settings. */
static void game_blit(uint32_t *win, int ww, int wh, int ax, int aw) {
    int dw, dh;
    if (aw < 120) return;
    double f = (double)aw / 240 < (double)wh / 160 ? (double)aw / 240 : (double)wh / 160;
    if (cfg.fit || f < 1) { dw = (int)(240 * f); dh = (int)(160 * f); }
    else { int s = (int)f; dw = 240 * s; dh = 160 * s; }
    int ox = ax + (aw - dw) / 2, oy = (wh - dh) / 2;
    for (int y = 0; y < dh; y++) {
        uint32_t *dst = win + (size_t)(oy + y) * ww + ox;
        if (!cfg.smooth) {
            const uint32_t *src = ppu_frame[y * 160 / dh];
            for (int x = 0; x < dw; x++) dst[x] = src[x * 240 / dw];
        } else {
            int fy = (int)(((y * 2 + 1) * 160 * 256LL) / (dh * 2)) - 128; /* the source row, with an 8-bit fraction */
            int y0 = fy >> 8, ty = fy & 255, y1 = y0 + 1;
            if (y0 < 0) { y0 = 0; ty = 0; }
            if (y1 > 159) y1 = 159;
            if (y0 > 159) y0 = 159;
            for (int x = 0; x < dw; x++) {
                int fx = (int)(((x * 2 + 1) * 240 * 256LL) / (dw * 2)) - 128;
                int x0 = fx >> 8, tx = fx & 255, x1 = x0 + 1;
                if (x0 < 0) { x0 = 0; tx = 0; }
                if (x1 > 239) x1 = 239;
                if (x0 > 239) x0 = 239;
                uint32_t a = ppu_frame[y0][x0], b = ppu_frame[y0][x1], c = ppu_frame[y1][x0], d = ppu_frame[y1][x1], out = 0;
                for (int sh = 0; sh < 24; sh += 8) {
                    int top = (int)(a >> sh & 255) * (256 - tx) + (int)(b >> sh & 255) * tx;
                    int bot = (int)(c >> sh & 255) * (256 - tx) + (int)(d >> sh & 255) * tx;
                    out |= (uint32_t)((top * (256 - ty) + bot * ty) >> 16) << sh;
                }
                dst[x] = out;
            }
        }
    }
}

/* The whole window (ww by wh pixels) into `win`: the backdrop, the game on the right and the drawer on the left. */
static void menu_compose(uint32_t *win, int ww, int wh) {
    double kk = (double)ww / 480 < (double)wh / 270 ? (double)ww / 480 : (double)wh / 270;
    int k = (int)(kk + 0.5);
    if (k < 1) k = 1;
    int cw = ww / k, ch = wh / k, dw = cw < 290 ? cw : 290;
    for (int i = 0; i < ww * wh; i++) win[i] = th_bg;
    game_blit(win, ww, wh, dw * k, ww - dw * k);
    static uint8_t *buf; static size_t cap;
    size_t need = (size_t)dw * ch * 3;
    if (need > cap) { free(buf); buf = malloc(need); cap = need; }
    Canvas c = {buf, dw, ch};
    drawer_draw(&c);
    for (int y = 0; y < ch * k && y < wh; y++) {
        const uint8_t *row = buf + (size_t)(y / k) * dw * 3;
        uint32_t *dst = win + (size_t)y * ww;
        for (int x = 0; x < dw * k; x++) { const uint8_t *p = row + (x / k) * 3; dst[x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
    }
}

/* ---- the keys ---- */

static void menu_key(SDL_Keycode k) {
    int rows = cat_rows(menu_tab);
    menu_dirty = 1; menu_free_scroll = 0;
    switch (k) {
    case SDLK_UP:
        if (menu_focus) { if (rows) menu_sel[menu_tab] = (menu_sel[menu_tab] + rows - 1) % rows; }
        else menu_tab = (menu_tab + n_cats - 1) % n_cats;
        break;
    case SDLK_DOWN:
        if (menu_focus) { if (rows) menu_sel[menu_tab] = (menu_sel[menu_tab] + 1) % rows; }
        else menu_tab = (menu_tab + 1) % n_cats;
        break;
    case SDLK_LEFT: if (menu_focus) menu_change(-1); break;
    case SDLK_RIGHT: if (menu_focus) menu_change(1); else menu_focus = 1; break;
    case SDLK_RETURN: case SDLK_SPACE: if (menu_focus) menu_change(0); else menu_focus = 1; break;
    case SDLK_BACKSPACE: menu_focus = 0; break;
    case SDLK_TAB: case SDLK_e: case SDLK_PAGEDOWN: menu_tab = (menu_tab + 1) % n_cats; break;
    case SDLK_q: case SDLK_PAGEUP: menu_tab = (menu_tab + n_cats - 1) % n_cats; break;
    }
}

/* A tap on the menu (touch screens): fx, fy in window pixels. A tap on the sidebar picks a category, on a row selects it and acts
   (the left half of a choice or a slider steps down, the right half up), outside the drawer closes. Returns 1 to close the menu. */
static int menu_touch(float fx, float fy) {
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    double kk = (double)ww / 480 < (double)wh / 270 ? (double)ww / 480 : (double)wh / 270;
    int k = (int)(kk + 0.5);
    if (k < 1) k = 1;
    int cw = ww / k, ch = wh / k, dw = cw < 290 ? cw : 290;
    int x = (int)(fx / k), y = (int)(fy / k), sw = 84, top = 54;
    menu_dirty = 1;
    if (x >= dw || (x >= dw - 40 && y < 40)) return 1;
    if (x < sw + 4) {
        int t = (y - top) / (MROW + 2);
        if (y >= top && t >= 0 && t < n_cats) { menu_tab = t; menu_focus = 0; menu_free_scroll = 0; }
        return 0;
    }
    int rh = MROW, vis = (ch - top - 62) / rh, px = sw + 16, pw = dw - px - 12;
    if (vis < 1) vis = 1;
    int r = menu_off[menu_tab] + (y - top) / rh;
    if (y < top || r < 0 || r >= cat_rows(menu_tab) || (y - top) / rh >= vis) return 0;
    menu_sel[menu_tab] = r;
    menu_focus = 1; menu_free_scroll = 0;
    char val[48]; int kind, pct;
    row_value(menu_tab, r, val, sizeof val, &kind, &pct);
    if (kind == 4) menu_change(1); /* the whole row is the switch */
    else if (kind == 2) { /* the bar: a tap left of its end lowers the value, right of it raises it */
        int sx = px + pw - 8 - 30 - 52, ends = sx + 44 * pct / 100;
        menu_change(x < ends ? -1 : 1);
    } else if (kind == 1) menu_change(x < px + pw - 40 && x > px + pw / 2 ? -1 : 1); /* a choice: tapping cycles forward; near its left arrow goes back */
    else menu_change(0);
    return 0;
}

/* A finger dragged over the rows scrolls them when there are more than fit (x, dy in 0..1 of the window). */
static void menu_drag(float fx, float fdy) {
    static float acc;
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    double kk = (double)ww / 480 < (double)wh / 270 ? (double)ww / 480 : (double)wh / 270;
    int k = (int)(kk + 0.5); if (k < 1) k = 1;
    int top = 54, rh = MROW, vis = (wh / k - top - 62) / rh, nrows = cat_rows(menu_tab);
    if (fx * ww / k < 90 || nrows <= vis) return;
    acc -= fdy * wh / k;
    int step = (int)(acc / rh);
    if (!step) return;
    acc -= step * rh;
    int *off = &menu_off[menu_tab];
    *off += step;
    if (*off > nrows - vis) *off = nrows - vis;
    if (*off < 0) *off = 0;
    menu_free_scroll = 1; menu_dirty = 1;
}

/* Runs the menu until it closes (Esc in the sidebar, F1, Start on a controller). Returns 1 when the user chose Quit. */
static int menu_run(void) {
    SDL_Texture *mt = NULL; int mw = 0, mh = 0;
    static uint32_t *win; static size_t cap;
    if (audio) SDL_ClearQueuedAudio(audio);
    SDL_RenderSetLogicalSize(ren, 0, 0);
    menu_quit = 0; menu_dirty = 1; menu_close_req = 0;
    for (int open = 1; open && !menu_quit && !menu_close_req;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) menu_quit = 1;
            else if (e.type == SDL_WINDOWEVENT) menu_dirty = 1;
            else if (menu_capture >= 0) { /* waiting for the key or the button for a control */
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_ESCAPE) { menu_capture = -1; menu_dirty = 1; }
                    else if (e.key.keysym.scancode != SDL_SCANCODE_F1 && e.key.keysym.scancode != SDL_SCANCODE_F11) bind_set(menu_capture, 0, e.key.keysym.scancode);
                } else if (e.type == SDL_CONTROLLERBUTTONDOWN) bind_set(menu_capture, 1, e.cbutton.button);
            }
            else if (e.type == SDL_FINGERMOTION) menu_drag(e.tfinger.x, e.tfinger.dy);
            else if (e.type == SDL_FINGERDOWN) { int ww, wh; SDL_GetRendererOutputSize(ren, &ww, &wh); if (menu_touch(e.tfinger.x * ww, e.tfinger.y * wh)) open = 0; }
            else if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_F1 || k == SDLK_AC_BACK) open = 0;
                else if (k == SDLK_ESCAPE) { if (menu_focus) { menu_focus = 0; menu_dirty = 1; } else open = 0; }
                else if (k == SDLK_F11) { cfg.fullscreen = !cfg.fullscreen; apply_video(); SDL_RenderSetLogicalSize(ren, 0, 0); menu_dirty = 1; }
                else menu_key(k);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: menu_key(SDLK_UP); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: menu_key(SDLK_DOWN); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: menu_key(SDLK_LEFT); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: menu_key(SDLK_RIGHT); break;
                case SDL_CONTROLLER_BUTTON_A: menu_key(SDLK_RETURN); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: menu_key(SDLK_q); break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: menu_key(SDLK_e); break;
                case SDL_CONTROLLER_BUTTON_B: if (menu_focus) { menu_focus = 0; menu_dirty = 1; } else open = 0; break;
                case SDL_CONTROLLER_BUTTON_START: open = 0; break;
                }
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        }
        if (menu_dirty) {
            int ww, wh;
            SDL_GetRendererOutputSize(ren, &ww, &wh);
            if (ww != mw || wh != mh) {
                if (mt) SDL_DestroyTexture(mt);
                SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
                mt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, ww, wh);
                mw = ww; mh = wh;
            }
            if ((size_t)ww * wh > cap) { free(win); win = malloc((size_t)ww * wh * 4); cap = (size_t)ww * wh; }
            menu_compose(win, ww, wh);
            SDL_UpdateTexture(mt, NULL, win, ww * 4);
            menu_dirty = 0;
        }
        if (mt) { /* drawn every pass: presenting without drawing shows old buffers on Android */
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, mt, NULL, NULL);
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    if (mt) SDL_DestroyTexture(mt);
    cfg_save();
    apply_video(); /* back to the picture of the game: integer scale and the smoothing */
    SDL_RenderSetLogicalSize(ren, 240, 160);
    next_tick = 0; /* do not run to catch up with the time the menu took */
    return menu_quit;
}

/* --menu-shot FILE.ppm [--menu-tab N] [--menu-focus 1] [--menu-size WxH]: writes the window with the menu over the picture of frame 900 and quits (tests, screenshots). */
static const char *menu_shot_path;
static int menu_shot_w = 960, menu_shot_h = 640;
static void menu_shot(void) {
    uint32_t *win = malloc((size_t)menu_shot_w * menu_shot_h * 4);
    menu_compose(win, menu_shot_w, menu_shot_h);
    FILE *f = fopen(menu_shot_path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", menu_shot_w, menu_shot_h);
        for (int i = 0; i < menu_shot_w * menu_shot_h; i++) { uint8_t p[3] = {(uint8_t)(win[i] >> 16), (uint8_t)(win[i] >> 8), (uint8_t)win[i]}; fwrite(p, 1, 3, f); }
        fclose(f);
    }
    exit(0);
}
