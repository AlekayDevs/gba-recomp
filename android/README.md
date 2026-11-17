# App Android de gba-port

**Estado** (2026-11-17, tablet Samsung por USB): el teléfono **genera el C del juego desde la ROM que elige el usuario** y lo compila al arrancar; juega a ~60 fps con controles táctiles, menú táctil y sonido. En una tablet de gama alta: generar 0,8 s + compilar y enlazar con libtcc 1,9 s, cada vez que se abre el juego (no hay caché ni hace falta). **La ROM y nada derivado de ella van en el APK** (1,7 MB): el APK se puede repartir; la ROM se elige con el selector de documentos, se copia a la carpeta privada de la app y se comprueba su CRC32.

Cómo funciona: `libmain.so` lleva el runtime y el frontal (`runtime/*.c`, clang -O2), la biblioteca del generador (Rust, `src/capi.rs`: `gba_gen_generate`) y `android_main.c`. Éste lee `files/rom.gba`, el perfil y las raíces dinámicas (assets), genera el C a `files/gen.c`, lo compila en memoria con **libtcc** (rama `mob` de TinyCC, `libtcc.so` aparte por la LGPL) con `rt_entry`/`rt_dispatch` renombradas (`-Drt_entry=gen_entry`) y los símbolos del runtime dados con `tcc_add_symbol`, borra `gen.c` y arranca `play.c`. `GameActivity` copia los assets a `files/assets` y enseña el aviso de progreso.

## Construir e instalar

```
# gradle.properties: gbaPort.sdlDir = SDL2 2.32.x, gbaPort.tinycc = TinyCC (rama mob, sin configurar), gbaPort.profile = games/<id>.toml del juego
export ANDROID_HOME=~/Android JAVA_HOME=~/Android/jdk
cd android && gradle --offline assembleDebug      # construye antes el generador: cargo rustc --lib --target aarch64-linux-android (rustup target add aarch64-linux-android)
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Necesita NDK 27.2.12479018, CMake 3.22.1, SDL2 y TinyCC (fuentes hermanas) y Rust. El perfil del juego aporta `dynamic_roots.txt` (junto al perfil) y el C propio (`games/<id>/text.c`, `options.c`, que van en `libmain.so`). La ROM la pone el usuario con el lanzador (o `adb push` a `/sdcard/Android/data/<applicationId>/files/rom.gba`; con Git Bash en Windows, `MSYS_NO_PATHCONV=1`). Medir: `adb logcat -s gbaport SDL/APP` (tiempos de preparación y «N fps» cada 600 fotogramas).

## Controles táctiles (`runtime/play_touch.h`)

Botones redondos con aro dorado, cruz con borde dorado y botones superiores en píldora, con la paleta verde y oro del juego (los toma de `--theme`). Cruceta, A, B, L, R, Start y Select, más SAVE y LOAD (ranura actual) y REWIND (mantenido). Varios dedos a la vez; un toque rápido cuenta al menos un fotograma. Medir: `adb logcat -s SDL/APP` (cada 600 fotogramas, «gbaport: N fps»).

## Lanzador (`LauncherActivity`)

Pantalla de inicio con los colores del juego: «Elegir ROM» abre el selector de archivos de Android, comprueba el CRC32 (el del perfil del juego), copia la ROM a la carpeta de la app y muestra «Jugar» / «Cambiar ROM». Una ROM que no es la esperada se rechaza con el CRC que tiene. Ya no hace falta `adb push`. Textos en español e inglés según el idioma del sistema.

## Actualizaciones

El lanzador busca la última release de GitHub (`gbaPort.updateUrl` en `gradle.properties`; vacía: la app no usa Internet) una vez al día si la casilla está activa, y hay un botón «Buscar actualizaciones». Si la release lleva un `.apk` y un `SHA256SUMS`, «Descargar e instalar» baja el APK, comprueba el hash y se lo da al instalador de Android (solo instala si la firma es la misma); si no, abre la página de la release. Para probarlo sin GitHub, en builds de depuración: `adb reverse tcp:8765 tcp:8765`, servir un JSON de release y `run-as <id> sh -c 'echo http://127.0.0.1:8765/latest > files/update_url'`.

## Falta

Firmar una versión, sonido comprobado a oído, probar en un móvil (también de gama baja y en vertical), `NOTICE.txt`.
