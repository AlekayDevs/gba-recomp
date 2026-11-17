package io.github.alekaydevs.gbaport;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.CRC32;

/**
 * Pantalla de inicio: pide la ROM del usuario, comprueba que es la esperada (CRC32), la copia a la carpeta de la app y abre el juego.
 * La ROM no sale del dispositivo. Colores del perfil del juego (verde del bosque y oro).
 * En esta prueba el C del juego ya va dentro del APK; la versión final lo generará aquí a partir de la ROM elegida.
 */
public class LauncherActivity extends Activity {
    // Datos del juego (del perfil games/minishcap_usa.toml)
    private static final String TITLE = "The Legend of Zelda\nThe Minish Cap";
    private static final long CRC = 0xABCEBBB1L;
    private static final int INK = 0xFFFFF8D8;
    private static final int REQ_ROM = 1;

    private final boolean es = Locale.getDefault().getLanguage().equals("es");
    private TextView status;
    private Pixel.Button play, pick;

    private String t(String en, String sp) { return es ? sp : en; }

    private File romFile() { return new File(getExternalFilesDir(null), "rom.gba"); }

    /** CRC32 de un fichero, o -1. */
    private static long crcOf(File f) {
        try (InputStream in = new FileInputStream(f)) {
            CRC32 c = new CRC32();
            byte[] b = new byte[1 << 16];
            for (int n; (n = in.read(b)) > 0; ) c.update(b, 0, n);
            return c.getValue();
        } catch (Exception e) {
            return -1;
        }
    }

    private Pixel.Button button(boolean primary) {
        Pixel.Button b = new Pixel.Button(this, primary);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        p.topMargin = 6 * Pixel.pixelScale(this);
        p.gravity = Gravity.CENTER_HORIZONTAL;
        b.setLayoutParams(p);
        return b;
    }

    @Override
    protected void onCreate(Bundle s) {
        super.onCreate(s);
        int u = Pixel.pixelScale(this);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        FrameLayout frame = new FrameLayout(this);
        frame.addView(new Pixel.Background(this), new FrameLayout.LayoutParams(-1, -1));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(10 * u, 10 * u, 10 * u, 10 * u);
        root.addView(new Pixel.Title(this), new LinearLayout.LayoutParams(-1, -2));

        Pixel.Panel panel = new Pixel.Panel(this);
        status = new TextView(this);
        status.setTextColor(INK);
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        panel.addView(status);
        LinearLayout.LayoutParams pp = new LinearLayout.LayoutParams(-2, -2);
        pp.topMargin = 4 * u;
        pp.gravity = Gravity.CENTER_HORIZONTAL;
        panel.setMinimumWidth(Math.min(getResources().getDisplayMetrics().widthPixels - 24 * u, 110 * u));
        root.addView(panel, pp);

        play = button(true);
        play.setLabel(t("Play", "Jugar"));
        play.setOnClickListener(v -> startActivity(new Intent(this, GameActivity.class)));
        root.addView(play);

        pick = button(false);
        pick.setOnClickListener(v -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            startActivityForResult(i, REQ_ROM);
        });
        root.addView(pick);

        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.addView(root, new ViewGroup.LayoutParams(-1, -1));
        frame.addView(sv, new FrameLayout.LayoutParams(-1, -1));
        setContentView(frame);
        refresh(null);
    }

    /** Pone los textos según haya ROM válida; err es un aviso a mostrar (o null). */
    private void refresh(String err) {
        File f = romFile();
        boolean ok = f.isFile() && crcOf(f) == CRC;
        play.setVisibility(ok ? View.VISIBLE : View.GONE);
        pick.setLabel(ok ? t("Change ROM", "Cambiar ROM") : t("Choose ROM", "Elegir ROM"));
        String msg = ok
            ? t("ROM ready (USA). It stays on this device.", "ROM lista (USA). Se queda en este dispositivo.")
            : t("Choose your own copy of the ROM (USA, .gba). It is only copied into this app and is not sent anywhere.",
                "Elige tu propia copia de la ROM (USA, .gba). Solo se copia dentro de esta app y no se envía a ningún sitio.");
        status.setText(err != null ? err + "\n\n" + msg : msg);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_ROM || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri u = data.getData();
        File tmp = new File(getExternalFilesDir(null), "rom.tmp");
        CRC32 c = new CRC32();
        try (InputStream in = getContentResolver().openInputStream(u); OutputStream out = new FileOutputStream(tmp)) {
            byte[] b = new byte[1 << 16];
            long total = 0;
            for (int n; (n = in.read(b)) > 0; ) {
                c.update(b, 0, n);
                out.write(b, 0, n);
                total += n;
                if (total > (32 << 20)) throw new Exception("too big");
            }
        } catch (Exception e) {
            tmp.delete();
            refresh(t("Could not read the file: ", "No se pudo leer el archivo: ") + e.getMessage());
            return;
        }
        if (c.getValue() != CRC) {
            tmp.delete();
            refresh(String.format(t("This is not the expected ROM (CRC32 %08X, expected %08X). Use the USA version.",
                                    "Esta no es la ROM esperada (CRC32 %08X, esperada %08X). Usa la versión USA."), c.getValue(), CRC));
            return;
        }
        File dst = romFile();
        dst.delete();
        if (!tmp.renameTo(dst)) { tmp.delete(); refresh(t("Could not save the ROM.", "No se pudo guardar la ROM.")); return; }
        refresh(null);
    }
}
