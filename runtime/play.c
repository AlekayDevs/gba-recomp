/* Plays the recompiled game in a window (SDL2): video, keyboard and game controller, frame pacing at 59.7275 Hz.
     play <rom.gba> [--scale N] [--fullscreen] [--nolimit]
   Keys: arrows, X = A, Z = B, A = L, S = R, Enter = Start, Backspace = Select; Tab (held) = fast forward; F11 = full screen; F1 or Esc = menu (language, video, audio; Start+Back on a controller);
   F5 = save state, F8 = load state (slot 1..9 with the number keys; <rom>.sN; done at the next pass of the game's main loop);
   F12 = screenshot (shot-N.ppm); window close or Quit in the menu = quit. Controller: B/A = A/B, shoulders = L/R, Start, Back = Select, D-pad or left stick. */
#include "gba_sys.h"
#include <setjmp.h>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_entry(Cpu *c); /* generated: calls the function at the entry of the ROM */

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_GameController *pad;
static SDL_AudioDeviceID audio;
static int limit = 1, shots, slot = 1;
static char state_base[512];
static Uint64 next_tick, freq;

void sys_report(void) {} /* only the headless runner reports */

#include "play_menu.h"

static u32 key_state(void) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    u32 r = 0;
    r |= k[SDL_SCANCODE_X] ? 1 : 0;
    r |= k[SDL_SCANCODE_Z] ? 2 : 0;
    r |= (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) ? 4 : 0;
    r |= k[SDL_SCANCODE_RETURN] ? 8 : 0;
    r |= k[SDL_SCANCODE_RIGHT] ? 16 : 0;
    r |= k[SDL_SCANCODE_LEFT] ? 32 : 0;
    r |= k[SDL_SCANCODE_UP] ? 64 : 0;
    r |= k[SDL_SCANCODE_DOWN] ? 128 : 0;
    r |= k[SDL_SCANCODE_S] ? 256 : 0;
    r |= k[SDL_SCANCODE_A] ? 512 : 0;
    if (pad) {
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B) ? 1 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A) ? 2 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK) ? 4 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START) ? 8 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ? 16 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ? 32 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP) ? 64 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 128 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 256 : 0;
        r |= SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 512 : 0;
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        r |= ax > 16000 ? 16 : 0; r |= ax < -16000 ? 32 : 0; r |= ay < -16000 ? 64 : 0; r |= ay > 16000 ? 128 : 0;
    }
    return r;
}

static void on_frame(u32 frame) {
    if (menu_shot_path && frame == 900) menu_shot();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) exit(0);
        if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_F1)) { if (menu_run()) exit(0); }
        if (e.type == SDL_CONTROLLERBUTTONDOWN && pad && e.cbutton.button == SDL_CONTROLLER_BUTTON_START
            && SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) { if (menu_run()) exit(0); }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F11) {
            cfg.fullscreen = !cfg.fullscreen;
            SDL_SetWindowFullscreen(win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
            cfg_save();
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_9) {
            slot = e.key.keysym.sym - SDLK_0;
            char t[64]; snprintf(t, sizeof t, "gba-recomp - slot %d", slot); SDL_SetWindowTitle(win, t);
        }
        if (e.type == SDL_KEYDOWN && (e.key.keysym.sym == SDLK_F5 || e.key.keysym.sym == SDLK_F8) && !rt_state_req) {
            snprintf(rt_state_path, sizeof rt_state_path, "%s.s%d", state_base, slot);
            rt_state_req = e.key.keysym.sym == SDLK_F5 ? 1 : 2;
        }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12) {
            char name[64];
            snprintf(name, sizeof name, "shot-%d.ppm", ++shots);
            if (ppu_write_ppm(name)) printf("screenshot %s\n", name);
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(e.cdevice.which);
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) { SDL_GameControllerClose(pad); pad = NULL; }
    }
    if (rt_state_msg) {
        char t[96]; snprintf(t, sizeof t, "gba-recomp - slot %d: %s", slot, rt_state_msg);
        SDL_SetWindowTitle(win, t); printf("slot %d: %s
", slot, rt_state_msg); rt_state_msg = NULL;
    }
    /* sound: the samples of this frame go to the device; when it is far ahead (fast forward, a pause) they are dropped */
    if (audio) {
        if (SDL_GetQueuedAudioSize(audio) < 32768 * 4 / 5) {
            if (cfg.volume >= 100) SDL_QueueAudio(audio, apu_out, apu_n * 4);
            else {
                static short scaled[8192 * 2];
                int n = apu_n * 2 > 8192 * 2 ? 8192 * 2 : apu_n * 2;
                const short *src = (const short *)apu_out;
                for (int i = 0; i < n; i++) scaled[i] = (short)(src[i] * cfg.volume / 100);
                SDL_QueueAudio(audio, scaled, n * 2);
            }
        }
    }
    apu_n = 0;
    sys_keys = key_state();
    SDL_UpdateTexture(tex, NULL, ppu_frame, 240 * 4);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    /* pace: 280896 cycles at 16.78 MHz per frame; Tab = fast forward */
    int fast = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB];
    Uint64 now = SDL_GetPerformanceCounter(), period = (Uint64)((double)freq * 280896.0 / 16777216.0);
    if (!next_tick) next_tick = now;
    next_tick += period;
    if (limit && !fast) {
        if (next_tick > now) {
            Uint64 wait_ms = (next_tick - now) * 1000 / freq;
            if (wait_ms > 1) SDL_Delay((Uint32)(wait_ms - 1));
            while (SDL_GetPerformanceCounter() < next_tick) {}
        } else if (now - next_tick > period * 5) {
            next_tick = now; /* far behind (window dragged, a pause): do not run to catch up */
        }
    } else {
        next_tick = now;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: play <rom.gba> [--scale N] [--fullscreen] [--nolimit]\n"); return 2; }
    int scale = 4, full = 0;
    const char *saves_dir = NULL, *title = "gba-recomp", *cli_lang = "";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--saves") && i + 1 < argc) saves_dir = argv[++i];
        else if (!strcmp(argv[i], "--title") && i + 1 < argc) title = argv[++i];
        else if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) snprintf(game_dir, sizeof game_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--language") && i + 1 < argc) cli_lang = argv[++i];
        else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) menu_shot_path = argv[++i];
        else if (!strcmp(argv[i], "--menu-tab") && i + 1 < argc) menu_tab = atoi(argv[++i]) % N_CATS;
        else if (!strcmp(argv[i], "--menu-focus") && i + 1 < argc) menu_focus = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--menu-size") && i + 1 < argc) sscanf(argv[++i], "%dx%d", &menu_shot_w, &menu_shot_h);
        else if (!strcmp(argv[i], "--brand") && i + 1 < argc) snprintf(brand, sizeof brand, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--theme") && i + 1 < argc) theme_parse(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) full = 1;
        else if (!strcmp(argv[i], "--nolimit")) limit = 0;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    rt_rom_len = (u32)ftell(f);
    fseek(f, 0, SEEK_SET);
    rt_rom = malloc(rt_rom_len);
    if (fread(rt_rom, 1, rt_rom_len, f) != rt_rom_len) return 1;
    fclose(f);
    /* settings: config.ini in the saves folder (or next to the ROM); a --language on the command line wins and is kept */
    emblem_load();
    snprintf(cfg_path, sizeof cfg_path, "%s/config.ini", saves_dir ? saves_dir : ".");
    cfg_load();
    if (cli_lang[0] && strcmp(cli_lang, "dump")) { int l = lang_index(cli_lang); if (l != cfg.lang) { cfg.lang = l; cfg_save(); } }
    if (full) cfg.fullscreen = 1;
    rom_orig = malloc(rt_rom_len);
    memcpy(rom_orig, rt_rom, rt_rom_len);
    rt_game_init(game_dir, !strcmp(cli_lang, "dump") ? "dump" : langs[cfg.lang].code);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 240 * scale, 160 * scale,
                           SDL_WINDOW_RESIZABLE);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED) : NULL;
    if (!ren && win) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, 240, 160);
    apply_video();
    {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = 32768; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio) SDL_PauseAudioDevice(audio, 0); else fprintf(stderr, "no audio: %s
", SDL_GetError());
    }
    freq = SDL_GetPerformanceFrequency();
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    {
        /* the save file goes next to the ROM (<rom without .gba>.sav) or, with --saves DIR, into that folder under the
           ROM's name; a save found next to the ROM is copied there the first time */
        char sav[512], old[512];
        snprintf(sav, sizeof sav, "%s", argv[1]);
        char *dot = strrchr(sav, '.');
        if (dot && (!strchr(dot, '/')) && (!strchr(dot, 92))) *dot = 0;
        snprintf(old, sizeof old, "%s.sav", sav);
        if (saves_dir) {
            const char *name = strrchr(sav, '/'); const char *bs = strrchr(sav, 92);
            if (bs && (!name || bs > name)) name = bs;
            name = name ? name + 1 : sav;
            char moved[512];
            snprintf(moved, sizeof moved, "%s/%s", saves_dir, name);
            snprintf(sav, sizeof sav, "%s", moved);
        }
        snprintf(state_base, sizeof state_base, "%s", sav);
        strncat(sav, ".sav", sizeof sav - strlen(sav) - 1);
        if (saves_dir) {
            FILE *have = fopen(sav, "rb");
            if (have) fclose(have);
            else {
                FILE *src = fopen(old, "rb");
                if (src) {
                    FILE *dst = fopen(sav, "wb");
                    if (dst) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, src)) > 0) fwrite(buf, 1, n, dst); fclose(dst); }
                    fclose(src);
                }
            }
        }
        save_init(sav);
    }
    sys_max_frames = 0;
    sys_frame_hook = on_frame;
    static Cpu cpu;
    rt_reset_armed = 1;
    setjmp(rt_reset_jb); /* SoftReset comes back here */
    sys_init(&cpu);
    rt_entry(&cpu);
    return 0;
}
