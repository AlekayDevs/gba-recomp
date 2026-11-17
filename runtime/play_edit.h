/* Editor of the touch buttons (Android): drag a button to move it, and set its size and opacity. Opened from Controls > Edit buttons.
   The game stays visible behind; the panel in the middle works on the chosen button. Included by play.c after play_touch.h. */

static void touch_edit_run(void) {
    SDL_Texture *mt = NULL; int mw = 0, mh = 0;
    static uint32_t *win; static size_t cap;
    int sel = -1; SDL_FingerID drag = -1; int dragging = 0, done = 0;
    enum { PW = 190, PH = 84 }; /* the panel, in interface pixels */
    if (audio) SDL_ClearQueuedAudio(audio);
    SDL_RenderSetLogicalSize(ren, 0, 0);
    touch_editing = 1;
    while (!done) {
        int ww, wh;
        SDL_GetRendererOutputSize(ren, &ww, &wh);
        double kk = (double)ww / 480 < (double)wh / 270 ? (double)ww / 480 : (double)wh / 270;
        int k = (int)(kk + 0.5); if (k < 1) k = 1;
        int px0 = (ww / k - PW) / 2, py0 = (wh / k - PH) / 2; /* the panel's corner, interface pixels */
        /* the hit boxes of the panel: size -, size +, opacity -, opacity +, Reset, Done (x, y, w, h relative to the panel) */
        static const int hb[6][4] = {{100, 22, 20, 14}, {160, 22, 20, 14}, {100, 42, 20, 14}, {160, 42, 20, 14}, {10, 62, 80, 16}, {100, 62, 80, 16}};
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT || e.type == SDL_APP_WILLENTERBACKGROUND) { SDL_PushEvent(&e); done = 1; break; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_AC_BACK) done = 1;
            if (e.type == SDL_FINGERDOWN) {
                float x = e.tfinger.x * ww, y = e.tfinger.y * wh;
                int ix = (int)(x / k) - px0, iy = (int)(y / k) - py0, hit = -1;
                if (ix >= 0 && iy >= 0 && ix < PW && iy < PH) {
                    for (int h = 0; h < 6; h++) if (ix >= hb[h][0] && ix < hb[h][0] + hb[h][2] && iy >= hb[h][1] && iy < hb[h][1] + hb[h][3] + 4) hit = h;
                    if (hit >= 0 && hit < 4 && sel >= 0) {
                        int d = (hit & 1) ? 10 : -10;
                        if (hit < 2) { cfg.tb_sc[sel] += d; if (cfg.tb_sc[sel] < 50) cfg.tb_sc[sel] = 50; if (cfg.tb_sc[sel] > 200) cfg.tb_sc[sel] = 200; }
                        else { cfg.tb_op[sel] += d; if (cfg.tb_op[sel] < 20) cfg.tb_op[sel] = 20; if (cfg.tb_op[sel] > 100) cfg.tb_op[sel] = 100; }
                    } else if (hit == 4) {
                        if (sel >= 0) { cfg.tb_dx[sel] = cfg.tb_dy[sel] = 0; cfg.tb_sc[sel] = cfg.tb_op[sel] = 100; }
                        else touch_layout_reset();
                    } else if (hit == 5) done = 1;
                } else {
                    int pick = -1;
                    for (int i = TB_N - 1; i >= 0 && pick < 0; i--) {
                        if (!cfg.t_quick && (i == TB_SAVE || i == TB_LOAD || i == TB_REWIND)) continue;
                        if (touch_hit(&tbtn[i], x, y)) pick = i;
                    }
                    if (pick >= 0) { sel = pick; drag = e.tfinger.fingerId; dragging = 1; }
                    else sel = -1;
                }
            } else if (e.type == SDL_FINGERMOTION && dragging && e.tfinger.fingerId == drag && sel >= 0) {
                cfg.tb_dx[sel] += e.tfinger.dx; cfg.tb_dy[sel] += e.tfinger.dy;
            } else if (e.type == SDL_FINGERUP && e.tfinger.fingerId == drag) dragging = 0;
        }
        if (done) break;
        touch_layout();
        if (ww != mw || wh != mh) {
            if (mt) SDL_DestroyTexture(mt);
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            mt = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, ww, wh);
            mw = ww; mh = wh;
        }
        if ((size_t)ww * wh > cap) { free(win); win = malloc((size_t)ww * wh * 4); cap = (size_t)ww * wh; }
        for (int i = 0; i < ww * wh; i++) win[i] = th_bg;
        game_blit(win, ww, wh, 0, ww);
        for (int i = 0; i < ww * wh; i++) win[i] = ((win[i] >> 1) & 0x7F7F7F) + ((th_bg >> 1) & 0x7F7F7F); /* the picture dimmed */
        static uint8_t buf[PW * PH * 3];
        Canvas c = {buf, PW, PH};
        for (int j = 0; j < PH; j++) cv_rect(&c, 0, j, PW, 1, ui_mix(th_panel, th_bg, j, PH - 1));
        cv_rect(&c, 0, 0, PW, 1, th_gold); cv_rect(&c, 0, PH - 1, PW, 1, th_gold); cv_rect(&c, 0, 0, 1, PH, th_gold); cv_rect(&c, PW - 1, 0, 1, PH, th_gold);
        char val[32];
        text_draw(&c, 10, 8, tr(sel >= 0 ? (tbtn[sel].label[0] ? tbtn[sel].label : "PAD") : "Drag a button"), th_gold, 1);
        text_draw(&c, 10, 26, tr("Size"), th_ink, 1);
        text_draw(&c, 10, 46, tr("Opacity"), th_ink, 1);
        for (int h = 0; h < 4; h++) {
            cv_rect(&c, hb[h][0], hb[h][1], hb[h][2], hb[h][3], sel >= 0 ? th_gold : ui_mix(th_ink, th_bg, 1, 3));
            cv_rect(&c, hb[h][0] + 1, hb[h][1] + 1, hb[h][2] - 2, hb[h][3] - 2, th_bg);
            text_draw(&c, hb[h][0] + 8, hb[h][1] + 3, (h & 1) ? "+" : "-", th_ink, 0);
        }
        if (sel >= 0) {
            snprintf(val, sizeof val, "%d%%", cfg.tb_sc[sel]); text_draw(&c, 126, 26, val, th_ink, 1);
            snprintf(val, sizeof val, "%d%%", cfg.tb_op[sel]); text_draw(&c, 126, 46, val, th_ink, 1);
        }
        for (int h = 4; h < 6; h++) {
            cv_rect(&c, hb[h][0], hb[h][1], hb[h][2], hb[h][3], th_gold);
            cv_rect(&c, hb[h][0] + 1, hb[h][1] + 1, hb[h][2] - 2, hb[h][3] - 2, th_bg);
            const char *t = tr(h == 4 ? "Reset" : "Done");
            text_draw(&c, hb[h][0] + (hb[h][2] - text_w(t)) / 2, hb[h][1] + 4, t, h == 5 ? th_gold : th_ink, 1);
        }
        for (int y = 0; y < PH * k && py0 * k + y < wh; y++) {
            const uint8_t *row = buf + (size_t)(y / k) * PW * 3;
            uint32_t *dst = win + (size_t)(py0 * k + y) * ww + px0 * k;
            for (int x = 0; x < PW * k && px0 * k + x < ww; x++) { const uint8_t *p = row + (x / k) * 3; dst[x] = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]; }
        }
        SDL_UpdateTexture(mt, NULL, win, ww * 4);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, mt, NULL, NULL);
        touch_draw(0);
        SDL_RenderSetLogicalSize(ren, 0, 0);
        if (sel >= 0) { /* the box of the chosen button */
            const TouchBtn *b = &tbtn[sel];
            float hw = b->shape == 1 ? b->w / 2 : b->w, hh = b->shape == 1 ? b->h / 2 : b->h;
            SDL_SetRenderDrawColor(ren, (th_gold >> 16) & 255, (th_gold >> 8) & 255, th_gold & 255, 255);
            for (int t = 0; t < 3; t++) { SDL_Rect r = {(int)(b->cx - hw) - t, (int)(b->cy - hh) - t, (int)(2 * hw) + 2 * t, (int)(2 * hh) + 2 * t}; SDL_RenderDrawRect(ren, &r); }
        }
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    touch_editing = 0;
    if (mt) SDL_DestroyTexture(mt);
    cfg_save();
    apply_video();
    SDL_RenderSetLogicalSize(ren, 240, 160);
    next_tick = 0;
}
