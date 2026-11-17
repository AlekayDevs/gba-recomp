package io.github.alekaydevs.gbaport;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.widget.LinearLayout;

/**
 * Pixel art del lanzador, dibujado en código (sin imágenes): una fuente de 5x7, placas con marco de oro biselado, la gorra de Ezlo y un
 * fondo de bosque con luciérnagas. Todo es de elaboración propia; coincide con los botones táctiles del juego (runtime/play_touch.h).
 */
final class Pixel {
    static final int OUTLINE = 0xFF14240C, GOLD_HI = 0xFFFFF0A0, GOLD = 0xFFF0C840, GOLD_LO = 0xFFB07818, GOLD_DK = 0xFF684010;
    static final int STONE = 0xFF1E4A30, STONE_HI = 0xFF2F6E46, PRESS = 0xFF58B868, TEXT = 0xFFFFF8D8, SHADOW = 0xFF0C2014;

    private static final String ORDER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    private static final int[][] FONT = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
        {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
        {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    };
    private static final String[] HAT = {
        "......gg.....", ".....gGgg....", "....gGgggg...", "...gGggggg...", "..gGgggggg...", ".gGggggggg...", ".gGgggggggg..",
        "gGgggggggggg.", "bBBBBBBBBBBb.", "bbbbbbbbbbbb.", ".bbbbbbbbbb..",
    };

    private static final Paint P = new Paint();
    static { P.setAntiAlias(false); P.setFilterBitmap(false); }

    static void rect(Canvas c, float l, float t, float r, float b, int col) {
        P.setColor(col);
        c.drawRect(l, t, r, b, P);
    }

    static int textWidth(String s, int p) { return (s.length() * 6 - 1) * p; }

    /** Texto en mayúsculas con sombra; x, y es la esquina superior izquierda y p los píxeles de pantalla por píxel de la fuente. */
    static void text(Canvas c, String s, float x, float y, int p, int col, int shadow) {
        for (int pass = 0; pass < 2; pass++) {
            float cx = x + (pass == 0 ? p : 0), cy = y + (pass == 0 ? p : 0);
            for (int i = 0; i < s.length(); i++, cx += 6 * p) {
                int g = ORDER.indexOf(Character.toUpperCase(s.charAt(i)));
                if (g < 0) continue;
                for (int gy = 0; gy < 7; gy++)
                    for (int gx = 0; gx < 5; gx++)
                        if ((FONT[g][gy] & (0x10 >> gx)) != 0) rect(c, cx + gx * p, cy + gy * p, cx + (gx + 1) * p, cy + (gy + 1) * p, pass == 0 ? shadow : col);
            }
        }
    }

    /** Un rectángulo con las esquinas cortadas un píxel. */
    static void cut(Canvas c, float l, float t, float r, float b, float k, int col) {
        rect(c, l + k, t, r - k, b, col);
        rect(c, l, t + k, r, b - k, col);
    }

    /** Placa con marco de oro: contorno oscuro, bisel (luz arriba a la izquierda), línea oscura y cuerpo. */
    static void plate(Canvas c, float l, float t, float r, float b, int p, int body, int bodyHi, boolean pressed) {
        cut(c, l, t, r, b, p, OUTLINE);
        cut(c, l + p, t + p, r - p, b - p, p, pressed ? GOLD_LO : GOLD_HI);
        rect(c, l + p, b - 2 * p, r - p, b - p, pressed ? GOLD_DK : GOLD_LO);   // borde inferior
        rect(c, r - 2 * p, t + p, r - p, b - p, pressed ? GOLD_DK : GOLD_LO);   // borde derecho
        rect(c, l + p, t + p, l + 2 * p, t + 2 * p, pressed ? GOLD_LO : GOLD);
        cut(c, l + 2 * p, t + 2 * p, r - 2 * p, b - 2 * p, 0, GOLD_DK);
        cut(c, l + 3 * p, t + 3 * p, r - 3 * p, b - 3 * p, 0, body);
        if (!pressed) rect(c, l + 3 * p, t + 3 * p, r - 3 * p, t + 4 * p, bodyHi);
    }

    /** La gorra de Ezlo (13x11 píxeles, con contorno oscuro) con la esquina superior izquierda en x, y. */
    static void hat(Canvas c, float x, float y, int p) {
        for (int pass = 0; pass < 2; pass++)
            for (int j = -1; j <= HAT.length; j++)
                for (int i = -1; i <= 13; i++) {
                    boolean in = j >= 0 && j < HAT.length && i >= 0 && i < 13;
                    char h = in ? HAT[j].charAt(i) : '.';
                    if (pass == 0) {
                        if (h != '.') continue;
                        boolean near = false;
                        int[][] d = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
                        for (int[] q : d) {
                            int jj = j + q[0], ii = i + q[1];
                            if (jj >= 0 && jj < HAT.length && ii >= 0 && ii < 13 && HAT[jj].charAt(ii) != '.') near = true;
                        }
                        if (near) rect(c, x + i * p, y + j * p, x + (i + 1) * p, y + (j + 1) * p, OUTLINE);
                    } else if (h != '.') {
                        int col = h == 'g' ? 0xFF3CC060 : h == 'G' ? 0xFFA0F0A0 : h == 'b' ? GOLD_LO : GOLD_HI;
                        rect(c, x + i * p, y + j * p, x + (i + 1) * p, y + (j + 1) * p, col);
                    }
                }
    }

    static int pixelScale(Context ctx) {
        int m = Math.min(ctx.getResources().getDisplayMetrics().widthPixels, ctx.getResources().getDisplayMetrics().heightPixels);
        return Math.max(2, Math.round(m / 150f));
    }

    /** Fondo: bosque en bandas con trama, luciérnagas y un marco de oro. */
    static final class Background extends View {
        private final int p = pixelScale(getContext());
        Background(Context c) { super(c); }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight();
            int[] bands = {0xFF0A1E18, 0xFF0D261E, 0xFF102E24, 0xFF14382A, 0xFF18422F};
            int bh = h / bands.length + 1;
            for (int i = 0; i < bands.length; i++) {
                rect(c, 0, i * bh, w, (i + 1) * bh, bands[i]);
                if (i + 1 < bands.length)   // trama de transición entre bandas
                    for (int x = 0; x < w; x += 4 * p) rect(c, x + ((x / (4 * p)) % 2 == 0 ? 0 : 2 * p), (i + 1) * bh - p, x + 2 * p + (x / (4 * p) % 2 == 0 ? 0 : 2 * p), (i + 1) * bh, bands[i + 1]);
            }
            long s = 12345;   // luciérnagas fijas
            for (int i = 0; i < 40; i++) {
                s = (s * 1103515245L + 12345L) & 0x7fffffffL;
                float x = (s % (w / p)) * p;
                s = (s * 1103515245L + 12345L) & 0x7fffffffL;
                float y = (s % (h / p)) * p;
                rect(c, x, y, x + p, y + p, i % 3 == 0 ? GOLD_HI : 0x80F0C840);
            }
            for (int x = 0; x < w; x += 8 * p) {   // hierba abajo
                int hh = 2 + (x / (8 * p)) % 3;
                rect(c, x, h - hh * p, x + 2 * p, h, 0xFF2E9E48);
                rect(c, x + 3 * p, h - (hh + 1) * p, x + 5 * p, h, 0xFF22783A);
            }
            rect(c, 0, h - 2 * p, w, h, 0xFF0A1E18);
            // marco
            float m = 3 * p;
            rect(c, m, m, w - m, m + p, GOLD_LO); rect(c, m, h - m - p, w - m, h - m, GOLD_LO);
            rect(c, m, m, m + p, h - m, GOLD_LO); rect(c, w - m - p, m, w - m, h - m, GOLD_LO);
            rect(c, m + 2 * p, m + 2 * p, w - m - 2 * p, m + 3 * p, GOLD); rect(c, m + 2 * p, h - m - 3 * p, w - m - 2 * p, h - m - 2 * p, GOLD);
            rect(c, m + 2 * p, m + 2 * p, m + 3 * p, h - m - 2 * p, GOLD); rect(c, w - m - 3 * p, m + 2 * p, w - m - 2 * p, h - m - 2 * p, GOLD);
        }
    }

    /** Gorra y título. */
    static final class Title extends View {
        private final int p = pixelScale(getContext());
        Title(Context c) { super(c); }

        @Override protected void onMeasure(int wm, int hm) {
            setMeasuredDimension(MeasureSpec.getSize(wm), (11 + 4) * p + 7 * (p / 2 + 1) + 6 * p + 7 * p + 4 * p);
        }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth();
            float y = 2 * p;
            hat(c, (w - 13 * p) / 2f, y, p);
            y += 15 * p;
            int p1 = Math.max(1, p / 2 + (p % 2));
            String a = "THE LEGEND OF ZELDA";
            text(c, a, (w - textWidth(a, p1)) / 2f, y, p1, GOLD_HI, SHADOW);
            y += 7 * p1 + 3 * p;
            String b = "THE MINISH CAP";
            int p2 = Math.min(p, Math.max(1, (w - 6 * p) / (6 * b.length())));
            text(c, b, (w - textWidth(b, p2)) / 2f, y, p2, GOLD, OUTLINE);
        }
    }

    /** Botón de placa dorada con letras de píxeles; el principal es dorado, el otro verde. */
    static final class Button extends View {
        private final int p = pixelScale(getContext());
        private String label = "";
        private final boolean primary;
        private boolean down;

        Button(Context c, boolean primary) {
            super(c);
            this.primary = primary;
            setClickable(true);
        }

        void setLabel(String s) { label = s; invalidate(); }

        @Override protected void onMeasure(int wm, int hm) {
            int w = Math.min(MeasureSpec.getSize(wm), Math.max(70 * p, textWidth(label, p) + 14 * p));
            setMeasuredDimension(w, (primary ? 22 : 17) * p);
        }

        @Override public boolean onTouchEvent(MotionEvent e) {
            boolean was = down;
            if (e.getAction() == MotionEvent.ACTION_DOWN) down = true;
            else if (e.getAction() == MotionEvent.ACTION_UP || e.getAction() == MotionEvent.ACTION_CANCEL) down = false;
            if (was != down) invalidate();
            return super.onTouchEvent(e);
        }

        @Override protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight(), off = down ? p : 0;
            int body = primary ? (down ? GOLD_LO : GOLD) : (down ? PRESS : STONE);
            plate(c, 0, off, w, h, p, body, primary ? GOLD_HI : STONE_HI, down);
            if (primary) rect(c, 3 * p, off + h - 5 * p, w - 3 * p, off + h - 4 * p, down ? GOLD_DK : GOLD_LO);
            int tp = p;   // si la etiqueta no cabe (columna estrecha), se dibuja con píxeles más pequeños
            while (tp > 1 && textWidth(label, tp) > w - 8 * p) tp--;
            text(c, label, (w - textWidth(label, tp)) / 2f, off + (h - 7 * tp) / 2f, tp, primary ? OUTLINE : GOLD_HI, primary ? GOLD_HI : SHADOW);
        }
    }

    /** Panel con marco para el texto de estado. */
    static final class Panel extends LinearLayout {
        private final int p = pixelScale(getContext());
        Panel(Context c, AttributeSet a) { super(c, a); init(); }
        Panel(Context c) { super(c); init(); }
        private void init() {
            setWillNotDraw(false);
            setOrientation(VERTICAL);
            setPadding(5 * p, 5 * p, 5 * p, 5 * p);
        }
        @Override protected void onDraw(Canvas c) {
            plate(c, 0, 0, getWidth(), getHeight(), Math.max(1, p / 2), 0xE00C2014, 0xE00C2014, false);
        }
    }
}
