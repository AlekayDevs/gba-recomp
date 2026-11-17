package io.github.alekaydevs.gbaport;

import android.content.res.AssetManager;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.FrameLayout;
import android.widget.TextView;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.Locale;

/**
 * Ejecuta el juego: copia a files/assets el perfil y las cabeceras (assets), y SDL arranca libmain.so (android_main.c), que genera el C del
 * juego desde files/rom.gba, lo compila con libtcc y llama al frontal de runtime/play.c. Mientras tanto se ve un aviso de progreso.
 */
public class GameActivity extends SDLActivity {
    private TextView progress;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "tcc", "main"};
    }

    @Override
    protected void attachBaseContext(android.content.Context base) {
        Lang.apply(base);
        super.attachBaseContext(base);
    }

    @Override
    protected void onCreate(Bundle state) {
        copyAssets();
        super.onCreate(state);
    }

    /** Copia los assets a files/assets (libtcc lee las cabeceras de disco). Se repite en cada arranque: es poco y asi una version nueva los cambia. */
    private void copyAssets() {
        try {
            AssetManager am = getAssets();
            StringBuilder sb = new StringBuilder();
            try (InputStream in = am.open("index.txt")) {
                byte[] b = new byte[8192];
                for (int n; (n = in.read(b)) > 0; ) sb.append(new String(b, 0, n, "UTF-8"));
            }
            for (String line : sb.toString().split("\n")) {
                if (line.isEmpty()) continue;
                File dst = new File(getFilesDir(), "assets/" + line);
                dst.getParentFile().mkdirs();
                try (InputStream in = am.open(line); FileOutputStream out = new FileOutputStream(dst)) {
                    byte[] b = new byte[65536];
                    for (int n; (n = in.read(b)) > 0; ) out.write(b, 0, n);
                }
            }
        } catch (Exception e) {
            android.util.Log.e("gbaport", "assets: " + e);
        }
    }

    /** Una vibración corta al pulsar un botón de la pantalla (la llama el runtime con JNI). */
    public void haptic() {
        android.os.Vibrator v = (android.os.Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (v != null && v.hasVibrator()) v.vibrate(android.os.VibrationEffect.createPredefined(android.os.VibrationEffect.EFFECT_TICK));
    }

    /** Llamado desde android_main.c: "gen" y "compile" son las fases, null lo quita. */
    public void setProgress(final String phase) {
        runOnUiThread(() -> {
            if (phase == null) {
                if (progress != null) { ((android.view.ViewGroup) progress.getParent()).removeView(progress); progress = null; }
                return;
            }
            boolean es = Locale.getDefault().getLanguage().equals("es");
            String t = phase.equals("gen") ? (es ? "Analizando la ROM…" : "Analysing the ROM…") : (es ? "Preparando el juego…" : "Getting the game ready…");
            if (progress == null) {
                progress = new TextView(this);
                progress.setTextColor(0xFFFFF8D8);
                progress.setBackgroundColor(0xE00C2014);
                progress.setTextSize(20);
                progress.setGravity(Gravity.CENTER);
                progress.setPadding(60, 40, 60, 40);
                addContentView(progress, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));
            }
            progress.setText(t);
        });
    }

    /** Pantalla completa de verdad (Android 15 pinta bajo las barras): se esconden las barras del sistema, salvo con un gesto desde el borde. */
    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (!hasFocus) return;
        android.view.WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.hide(android.view.WindowInsets.Type.systemBars());
            c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // SDL no admite un segundo SDL_main en el mismo proceso: al cerrar de verdad, el proceso muere.
        if (isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }
}
