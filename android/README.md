# App Android de gba-port (prueba)

**Estado: prueba que funciona** (2026-11-17, tablet Samsung por USB): arranca, juega a ~60 fps con controles táctiles y sonido. Falta que el teléfono genere el C del juego y el menú táctil.

En esta versión el C del juego se genera **en el PC** y se compila para arm64 dentro del APK. Ese C deriva de la ROM: **el APK no se reparte ni se sube**. La versión para repartir hará como `snes-recomp/android`: biblioteca del generador (Rust) y TinyCC en el dispositivo, con la ROM elegida por el usuario.

## Construir e instalar

```
gba-recomp gen <rom.gba> <gen.c> --pointers --profile ../minishcap-recomp/games/minishcap_usa.toml
# gradle.properties: gbaPort.genC = ruta del gen.c, gbaPort.sdlDir = SDL2 2.32.x, gbaPort.gameDir = games/<id> del juego
export ANDROID_HOME=~/Android JAVA_HOME=~/Android/jdk
cd android && gradle --offline assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell mkdir -p /sdcard/Android/data/<applicationId>/files
adb push <rom.gba> /sdcard/Android/data/<applicationId>/files/rom.gba
adb shell am start -n <applicationId>/io.github.alekaydevs.gbaport.GameActivity
```

Con Git Bash en Windows, `MSYS_NO_PATHCONV=1` evita que cambie las rutas `/sdcard/...`. Necesita NDK 27.2.12479018, CMake 3.22.1 y SDL2 (fuentes) como en snes-recomp. El C generado se compila con clang (`-O1`, unos 2 minutos).

## Controles táctiles (`runtime/play_touch.h`)

Botones redondos con aro dorado, cruz con borde dorado y botones superiores en píldora, con la paleta verde y oro del juego (los toma de `--theme`). Cruceta, A, B, L, R, Start y Select, más SAVE y LOAD (ranura actual) y REWIND (mantenido). Varios dedos a la vez; un toque rápido cuenta al menos un fotograma. Medir: `adb logcat -s SDL/APP` (cada 600 fotogramas, «gbaport: N fps»).

## Lanzador (`LauncherActivity`)

Pantalla de inicio con los colores del juego: «Elegir ROM» abre el selector de archivos de Android, comprueba el CRC32 (el del perfil del juego), copia la ROM a la carpeta de la app y muestra «Jugar» / «Cambiar ROM». Una ROM que no es la esperada se rechaza con el CRC que tiene. Ya no hace falta `adb push`. Textos en español e inglés según el idioma del sistema.

## Falta

Generar y compilar el C en el dispositivo, menú del reproductor con el dedo (ahora solo con teclado externo), icono y nombre propios, firmar una versión, sonido comprobado a oído, ajuste del tamaño y la posición de los botones.
