package io.github.alekaydevs.gbaport;

import org.libsdl.app.SDLActivity;

/** Ejecuta el juego: SDL arranca libmain.so (android_main.c), que llama al frontal de runtime/play.c con files/rom.gba. */
public class GameActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
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
