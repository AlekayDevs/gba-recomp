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

import android.app.AlertDialog;
import android.os.Handler;
import android.os.Looper;
import android.widget.CheckBox;
import android.widget.ProgressBar;
import java.io.File;
import java.io.IOException;
import java.util.concurrent.atomic.AtomicBoolean;
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
    private Pixel.Button play, pick, licences, check;
    private CheckBox auto;

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

        licences = button(false);
        licences.setLabel(t("Licences", "Licencias"));
        licences.setOnClickListener(v -> showLicences());
        if (UpdateChecker.available(this)) {
            auto = new CheckBox(this);
            auto.setText(t("Check for updates on start (uses the Internet)", "Buscar actualizaciones al empezar (usa Internet)"));
            auto.setTextColor(INK);
            auto.setChecked(UpdateChecker.enabled(this));
            auto.setOnCheckedChangeListener((b, on) -> UpdateChecker.setEnabled(this, on));
            LinearLayout.LayoutParams ap = new LinearLayout.LayoutParams(-2, -2);
            ap.topMargin = 6 * u;
            ap.gravity = Gravity.CENTER_HORIZONTAL;
            root.addView(auto, ap);
            check = button(false);
            check.setLabel(t("Check for updates", "Buscar actualizaciones"));
            check.setOnClickListener(v -> checkNow());
            root.addView(check);
        }

        root.addView(licences);

        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.addView(root, new ViewGroup.LayoutParams(-1, -1));
        frame.addView(sv, new FrameLayout.LayoutParams(-1, -1));
        setContentView(frame);
        refresh(null);
        if (UpdateChecker.enabled(this)) autoCheck();
    }

    /** Una consulta al día (si no, lo último que se supo); si hay versión nueva que no se omitió, pregunta. */
    private void autoCheck() {
        if (UpdateChecker.fresh(this)) {
            offer(UpdateChecker.fromCache(this));
            return;
        }
        Handler ui = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            UpdateChecker.Result r = UpdateChecker.query(this);
            ui.post(() -> offer(r));
        }).start();
    }

    private void offer(UpdateChecker.Result r) {
        if (isFinishing() || isDestroyed()) return;
        if (r.ok && r.newer && !UpdateChecker.skipped(this, r.tag)) showUpdateDialog(r);
    }

    private String newVersionText(UpdateChecker.Result r) {
        return String.format(t("There is a new version: %1$s (you have %2$s).", "Hay una versión nueva: %1$s (tienes %2$s)."), r.tag, UpdateChecker.installedVersion(this));
    }

    /** «Descargar e instalar» baja el APK de la release, comprueba su hash y lo instala; sin APK en la release abre su página. */
    private void showUpdateDialog(UpdateChecker.Result r) {
        new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
            .setTitle(t("New version", "Versión nueva"))
            .setMessage(newVersionText(r))
            .setPositiveButton(r.canInstall() ? t("Download and install", "Descargar e instalar") : t("Open release page", "Abrir la página de la release"), (d, w) -> {
                if (r.canInstall()) installUpdate(r);
                else startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(r.url)));
            })
            .setNegativeButton(t("Not now", "Ahora no"), null)
            .setNeutralButton(t("Skip this version", "Omitir esta versión"), (d, w) -> UpdateChecker.skip(this, r.tag))
            .show();
    }

    /** Pide el permiso de instalar desde esta app si falta; si no, descarga con una barra de progreso y entrega el APK al sistema. */
    private void installUpdate(UpdateChecker.Result r) {
        if (!getPackageManager().canRequestPackageInstalls()) {
            new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                .setMessage(t("To install the update, Android needs you to allow this app to install apps. Tap Open settings, turn it on, then come back and try again.",
                              "Para instalar la actualización, Android necesita que permitas a esta app instalar aplicaciones. Pulsa Abrir ajustes, actívalo, vuelve y inténtalo de nuevo."))
                .setPositiveButton(t("Open settings", "Abrir ajustes"), (d, w) ->
                    startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:" + getPackageName()))))
                .setNegativeButton(t("Not now", "Ahora no"), null)
                .show();
            return;
        }
        ProgressBar bar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        bar.setMax(100);
        bar.setPadding(48, 32, 48, 0);
        AtomicBoolean cancel = new AtomicBoolean(false);
        AlertDialog dlg = new AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
            .setTitle(t("Downloading the update...", "Descargando la actualización..."))
            .setView(bar)
            .setCancelable(false)
            .setNegativeButton(t("Cancel", "Cancelar"), (d, w) -> cancel.set(true))
            .show();
        Handler ui = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            String error = null;
            try {
                File apk = UpdateInstaller.download(this, r, p -> ui.post(() -> bar.setProgress(p)), cancel);
                UpdateInstaller.install(this, apk);
            } catch (UpdateInstaller.Failure f) {
                error = f.text;
            } catch (IOException e) {
                error = t("The download failed. Check your connection and try again.", "La descarga falló. Revisa la conexión e inténtalo de nuevo.");
            }
            final String err = error;
            ui.post(() -> {
                dlg.dismiss();
                if (err != null) android.widget.Toast.makeText(this, err, android.widget.Toast.LENGTH_LONG).show();
            });
        }).start();
    }

    /** El botón: consulta ahora y dice en pantalla qué ha pasado. */
    private void checkNow() {
        check.setEnabled(false);
        status.setText(t("Checking for updates...", "Buscando actualizaciones..."));
        Handler ui = new Handler(Looper.getMainLooper());
        new Thread(() -> {
            UpdateChecker.Result r = UpdateChecker.query(this);
            ui.post(() -> {
                check.setEnabled(true);
                if (!r.ok) status.setText(t("Could not check. Check your connection and try again.", "No se pudo comprobar. Revisa la conexión e inténtalo de nuevo."));
                else if (!r.newer) status.setText(String.format(t("You are up to date (version %1$s).", "Estás al día (versión %1$s)."), UpdateChecker.installedVersion(this)));
                else {
                    status.setText(newVersionText(r));
                    showUpdateDialog(r);
                }
            });
        }).start();
    }

    /** Los avisos de terceros (LGPL de libtcc y zlib de SDL2) con el texto de las licencias, tal como van en los assets. */
    private void showLicences() {
        StringBuilder sb = new StringBuilder();
        try {
            for (String f : new String[] {"NOTICE.txt", "libtcc-LGPL-2.1.txt", "SDL2-zlib.txt"}) {
                try (InputStream in = getAssets().open("licenses/" + f)) {
                    byte[] b = new byte[8192];
                    for (int n; (n = in.read(b)) > 0; ) sb.append(new String(b, 0, n, "UTF-8"));
                }
                sb.append("\n\n----------------------------------------\n\n");
            }
        } catch (Exception e) {
            sb.append(t("Could not read the licences: ", "No se pudieron leer las licencias: ")).append(e.getMessage());
        }
        TextView tv = new TextView(this);
        tv.setText(sb.toString());
        tv.setTextColor(INK);
        tv.setTextSize(12);
        tv.setTypeface(android.graphics.Typeface.MONOSPACE);
        tv.setPadding(40, 30, 40, 30);
        tv.setBackgroundColor(0xFF0C2014);
        ScrollView sv = new ScrollView(this);
        sv.addView(tv);
        new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
            .setTitle(t("Licences", "Licencias")).setView(sv).setPositiveButton(t("Close", "Cerrar"), null).show();
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
