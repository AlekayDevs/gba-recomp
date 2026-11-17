package io.github.alekaydevs.gbaport;

import android.content.Context;
import android.content.SharedPreferences;
import java.io.File;
import java.io.FileOutputStream;
import java.util.Locale;

/** El idioma de la app: el del teléfono (inglés o español) hasta que el jugador lo cambia con el botón del globo. */
final class Lang {
    private Lang() {}

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("lang", Context.MODE_PRIVATE);
    }

    /** «en» o «es»: lo elegido, o si no, el del teléfono (español solo si el teléfono está en español). */
    static String get(Context c) {
        String v = prefs(c).getString("lang", "");
        if (v.equals("en") || v.equals("es")) return v;
        return Locale.getDefault().getLanguage().equals("es") ? "es" : "en";
    }

    /** Cambia entre inglés y español y lo aplica. */
    static void toggle(Context c) {
        prefs(c).edit().putString("lang", get(c).equals("es") ? "en" : "es").apply();
        apply(c);
    }

    /**
     * Pone el idioma elegido como el del proceso (los textos de la app salen de Locale.getDefault) y lo deja en files/lang.txt para
     * la parte nativa, que se lo pasa al juego. Se llama desde attachBaseContext de cada actividad.
     */
    static void apply(Context c) {
        String l = get(c);
        Locale.setDefault(new Locale(l));
        try (FileOutputStream out = new FileOutputStream(new File(c.getFilesDir(), "lang.txt"))) {
            out.write(l.getBytes("UTF-8"));
        } catch (Exception e) {
            // el juego usará su idioma por omisión
        }
    }
}
