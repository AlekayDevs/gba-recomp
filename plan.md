# Plan de gba-recomp

Estado a 2026-11-10. El motor es propio y estático (Rust), hermano de `snes-recomp`; el primer juego es *The Legend of Zelda: The Minish Cap* (USA), en `..\minishcap-recomp`. Este plan lleva lo del motor; lo específico del juego está en `..\minishcap-recomp\plan.md`. Investigación y alternativas: `docs\investigacion.md`.

## Hecho

- **El juego arranca y llega al título y a «CHOOSE A FILE» pulsando Start** (*Minish Cap*, sin ventana): traducción de Thumb y ARM a C (7095 de 7095 funciones), runtime con bus, E/S, DMA, temporizadores, interrupciones, BIOS por HLE y PPU por líneas; `tools/complete.sh` completa los saltos dinámicos que el análisis no ve (la ejecución los pide uno a uno). Pendiente: sonido, el resto de la BIOS y la jugabilidad.

- Lector de ROM: cabecera, comprobaciones, CRC32, espejos (`0x08/0x0A/0x0C`) y *overlays* (bloques que el arranque copia a RAM).
- Decodificadores ARM y Thumb: texto y flujo de control (`Flow`); 4 pruebas.
- Descubrimiento de código desde la entrada: `BX` con literal, `ADD Rd,PC` + `BX`, punteros Thumb de los datos, tablas de `switch` de Thumb.
- Con *Minish Cap* USA: 258 487 instrucciones (559 KB), 7088 funciones, 131 tablas de `switch`, 15 saltos indirectos dinámicos, 7 «indefinidas» (falsos positivos).
- Repositorios en GitHub (privados): `AlekayDevs/gba-recomp` y `AlekayDevs/minishcap-recomp`.

## 1. Analizador (lo que falta del descubrimiento)

- [x] **Validar los candidatos a función**: cada candidato de los datos se recorre aparte y solo se acepta si no llega a una instrucción indefinida ni sale de la ROM (`discover_with`). Con *Minish Cap*: 0 indefinidas, 7 candidatos descartados. Falta mirar que acabe en un retorno.
- [x] **Punteros a funciones ARM** (direcciones pares a un `STMFD SP!,{..,LR}`): 4 candidatos, ARM pasa de 341 a 886 instrucciones. Faltan las funciones ARM sin ese prólogo.
- [ ] **Separar código y datos**: los pools de literales y las tablas dentro del código se detectan por no alcanzarse; hay que marcarlos para no volver a tratarlos como código al recorrer linealmente y para no emitirlos.
- [ ] **Resolver más saltos**: `ldr rX,[rBase,rIndex]; bx rX` con una tabla de punteros (hay 2 en `0x0800129A` y `0x080012C4`); `bx r12` tras `mov r12, lr` (retorno, no salto); no confundir los `__call_via_rN` de libgcc (`0x08000EF2`..`0x08000F00`).
- [ ] **Tablas de `switch` de ARM** (el analizador solo reconoce las de Thumb) y las de otras formas (`TBB`-like no existen en ARMv4, pero hay variantes con `add pc, pc, r0, lsl #2`).
- [ ] **Más *overlays* automáticos**: hoy se pasan a mano (`--overlay`). Reconocer las rutinas de copia del arranque (`CpuSet`, `CpuFastSet`, DMA y bucles propios) y deducir origen, destino y tamaño; avisar si hay código que se ejecuta desde EWRAM/IWRAM sin overlay (el análisis lo ve como «fuera de la ROM»).
- [ ] **Código ejecutado desde RAM generado en ejecución** o que se modifica: confirmar que *Minish Cap* no lo hace (parece que solo copia bloques fijos).
- [ ] **Perfil del juego**: que el motor lea `games\<id>.toml` (hoy hay un borrador con los dos *overlays*): entradas, overlays, tablas de saltos conocidas, ganchos, nombres de símbolos propios.
- [x] **Mapa de la ROM** (`map`, `src/map.rs`): clasifica cada byte (cabecera, ARM, Thumb, código de *overlay*, pools de literales, tablas de `switch`, sin reclamar) y lista los huecos. Con *Minish Cap*: el código ocupa `0x08000000..0x080B197A` (727 KB): 76,7 % código, 7,0 % pools y tablas, 108 KB sin reclamar con contenido en 657 tramos, sobre todo cuatro bloques de datos (`0x08008B5C` 55 KB, `0x080046A4` 14 KB, `0x080029B4`, `0x0800137C`). Después de `0x080B2A70` hay 16 MB de datos (gráficos, mapas, texto). Falta distinguir tipos de datos dentro de los huecos y marcar los pools que la ruta lineal no debe tratar como código.

## 2. Generación de C

Spike hecho para Thumb (`src/gen.rs`, `runtime/`, `tools/spike.sh`, detalle en `docs/generacion-c.md`): 6782 de 7054 funciones traducidas, 14 MB de C que TinyCC compila en 1,4 s, y 0 diferencias contra Unicorn en 52 138 casos (3100 funciones). Las decisiones de abajo quedan así:

- [x] **Forma del C** (decidido: registros e indicadores locales, volcado a `Cpu` en llamadas y retornos, `goto` para saltos locales): una función C por función descubierta, con los registros ARM como variables locales (o una estructura `cpu`) y los indicadores N, Z, C, V explícitos y perezosos. Cuidar el rendimiento en Android (TinyCC en el móvil, como en alttp-recomp).
- [ ] **ARM y Thumb** (Thumb hecho en el spike, falta ARM): ejecución condicional de ARM; los cambios de modo con `BX` son llamadas o saltos entre funciones de estilo distinto.
- [ ] **Saltos indirectos**: despacho por dirección contra una tabla de funciones y bloques descubiertos, con un **respaldo** (intérprete ARM/Thumb) si la dirección no está, como hace `mstan/gbarecomp`; contar y registrar los fallos para ir ampliando el análisis.
- [ ] **Memoria**: los accesos a ROM, EWRAM, IWRAM, E/S, paleta, VRAM y OAM pasan por funciones de lectura/escritura del runtime (los de E/S y VRAM tienen efectos); los de EWRAM/IWRAM conviene inlinearlos.
- [ ] **Instrucciones con efectos de memoria raros**: `SWP`, `LDM/STM` con `^`, `LDR` sin alinear (rotación), `STR` de PC, accesos de byte a VRAM (se ignoran en parte), `MSR`/`MRS` y los cambios de modo del procesador (IRQ, SVC, SYS: bancos de registros).
- [ ] **Tiempo**: la GBA no tiene un contador de ciclos observable, pero el juego depende de VBlank/HBlank, temporizadores y DMA; decidir si se cuentan los ciclos (aproximados) o se avanza por eventos.
- [x] **Una prueba contra el original** (hecha con Unicorn, `tools/spike_diff.py`, no mGBA): compara registros, N Z C V y EWRAM/IWRAM tras ejecutar cada función. Límite: Unicorn no rota los accesos sin alinear. Falta una prueba contra un emulador de GBA completo (mGBA) para E/S, BIOS y temporización.

## 3. Runtime

En orden de aparición para llegar al título y a la partida:

- [ ] **Mapa de memoria** y espejos; BIOS de 16 KiB vacía o propia (no se redistribuye la oficial).
- [ ] **BIOS**: decidir entre HLE (propia, en el runtime) o recompilar la BIOS real, que no se puede repartir; la HLE es lo razonable. Llamadas que usa *Minish Cap* (SWI): `0` SoftReset, `1` RegisterRamReset, `3` Stop?, `5` VBlankIntrWait, `6` Div, `8` Sqrt, `11` CpuSet, `12` CpuFastSet, `14` BgAffineSet?, `15` ObjAffineSet, `17`/`18` descompresión LZ77, `25` SoundBias/SoundDriver..., y las de descompresión Huffman/RL si aparecen. Comprobar cada número con el desensamblado.
- [ ] **Interrupciones**: IME/IE/IF, VBlank, HBlank, temporizadores, DMA, teclado, serie; el manejador de la ROM se llama al entrar en la IRQ (la BIOS real lo llama desde `0x03007FFC`).
- [ ] **DMA** 0 a 3 (inmediato, VBlank, HBlank, sonido FIFO).
- [ ] **Temporizadores** 0 a 3 (el sonido directo los usa para el ritmo de muestreo).
- [ ] **PPU**: modos 0 a 2 (el juego usa sobre todo el 0), fondos con scroll, afines, sprites (normales, afines, doble tamaño), prioridades, mosaico, mezcla (alfa y brillo), ventanas, paletas, 15 bits de color. Primero por líneas, con efectos por línea (HBlank DMA/IRQ).
- [ ] **Sonido**: el juego lleva su propio motor M4A (código en la ROM y en IWRAM) que escribe en los registros de DMA directo A/B y en los 4 canales PSG; el runtime solo debe emular el hardware (FIFO, temporizadores, PSG, mezcla a 32768 Hz / 48 kHz).
- [x] **Guardado**: EEPROM de 512 B o 8 KiB (se detecta por la longitud de la primera petición DMA; *Minish Cap* usa 8 KiB, direcciones de 14 bits); `runtime/gba_save.c` escribe `<rom>.sav` tras cada escritura. Las lecturas directas de la CPU en `0x0D000000` devuelven el estado de la EEPROM (el juego las usa para esperar el fin de la escritura). Comprobado: se crea una partida («AAAAAA»), se guarda y reaparece al arrancar de nuevo.
- [x] **Entrada** (hecha en `runtime/play.c`: teclado y mando SDL2): teclado y mando (reutilizar el mapeo de snes-recomp, que ya trae mando, táctil y Android); `KEYINPUT`, `KEYCNT`.
- [~] **Ventana, vídeo y audio**: ventana y vídeo hechos (`runtime/play.c`, `tools/build_play.sh`: SDL2, escalado entero, 59,7275 Hz, Tab = avance rápido, F11, F12 = captura); falta el audio y la pantalla ancha.
- [x] **Estados guardados**: `runtime/gba_state.c`. Solo en el bucle principal (`[resume]` del perfil; Minish Cap `08055EC8`), donde el generador emite `if (rt_state_req) {STORE(); rt_state_point(c); RELOAD();}`. Guarda `Cpu`, EWRAM, IWRAM, paleta, VRAM, OAM, E/S y el estado de sys, PPU, APU y lectura de EEPROM; no la EEPROM (la partida del juego no se deshace). Comprobado: cargar un estado de otra línea temporal y correr 99 fotogramas da los mismos hashes de VRAM, paleta, OAM, EWRAM e IWRAM que la ejecución original. `play`: F5 guarda, F8 carga, 1-9 ranura (`<rom>.sN`); `run_headless --save-state/--load-state FRAME:fichero`. Falta: rebobinado, miniaturas, probar las teclas a mano.

- [x] **Menú del reproductor** (`runtime/play_menu.h`, piezas de `play_ui.h` copiadas de snes-recomp): F1 o Esc (Start+Back en el mando). Rediseñado (2026-11-17): cajón a la izquierda con el juego visible y en pausa a la derecha (con el escalado y el suavizado reales, así que Vídeo se ve al momento); lienzo de unos 480×270 «píxeles de interfaz» ampliado por un entero según la ventana, compuesto en software solo cuando algo cambia; barra lateral de categorías (Juego, Vídeo, Sonido), filas a la derecha, línea de descripción de la fila elegida y teclas al pie; colores, nombre y emblema del perfil (`gba-port` pasa `--brand` y `--theme` desde `[launcher]`; el emblema es `games/<id>/emblem.bmp`, 32×32, magenta transparente, que escribe `scripts/make_icon.py` del juego). Textos en inglés y español. Ajustes en `config.ini` de la carpeta de partidas. Cambiar el idioma restaura la ROM en memoria, vuelve a llamar a `game_init` y reinicia el juego. Probado con capturas (`--menu-shot FILE.ppm --menu-tab N --menu-focus 1 --menu-size WxH`) y con teclas inyectadas por SDL (navegar, cambiar, cerrar; el juego sigue). Falta probar a mano con teclado y mando reales. El juego queda en pausa mientras el menú está abierto.
  - **Controles** (2026-11-17): reasignar los 10 botones de la GBA, una tecla y un botón de mando por cada uno (Intro en la fila y luego pulsar; Esc cancela; si ya lo usa otro botón se intercambian), «Restaurar controles»; se guarda en `config.ini` (`key_<botón>` con el nombre de la tecla de SDL, `pad_<botón>` numérico). `play.c` lee `kb[]` y `pb[]` en vez de teclas fijas; la palanca izquierda sigue moviendo la cruceta. Probado inyectando teclas (asignar A=Q y que quede en `config.ini`).
  - **Estados** (2026-11-17): categoría con Ranura (1 a 9), Guardar estado y Cargar estado; piden lo mismo que F5/F8 y cierran el menú (el estado se escribe o carga en el siguiente paso del bucle principal). Probado inyectando teclas: elegir la ranura 2 y guardar escribe `<rom>.s2`. Falta el rebobinado y miniaturas/fecha por ranura.
  - **Idioma en el lanzador** (2026-11-17): botón «Idioma: English/Español» abajo a la derecha de la ventana de Windows (pantallas de elegir ROM y de jugar); alterna y escribe `language=` en `config.ini` de la carpeta de partidas (`port::set_config_language`, conserva el resto de líneas), que el reproductor lee al arrancar y que el menú del juego también cambia. Probado con la ventana real y un clic. Falta: otras lenguas cuando haya más traducciones (`port::GAME_LANGUAGES` y `langs[]` de `play_menu.h` deben coincidir).
  - **Rebobinado** (2026-11-17): mantener **R** (o L3) vuelve atrás un paso por fotograma, con una marca «REWIND» en la imagen; el ajuste Rebobinar (Estados) lo apaga. `runtime/gba_state.c`: cada 3 fotogramas (en el punto del bucle principal, como los estados) se serializa la máquina en memoria y se guarda la diferencia XOR con la anterior, con los ceros comprimidos; hasta 1200 pasos (un minuto) o 96 MB (en el título, 1200 pasos ocupan 1,4 MB). Pedidos nuevos de `rt_state_req`: 3 guarda en el historial y 4 retrocede. `run_headless --rewind-test FRAME:PASOS` guarda cada 3 fotogramas hasta FRAME y retrocede PASOS comprobando que la cadena de diferencias devuelve el estado exacto y que recargarlo y volver a serializarlo da los mismos bytes: 300 pasos sin diferencias con *Minish Cap*. Falta probar a mano en una mazmorra o con mucho movimiento (el tamaño de las diferencias crecerá) y el sonido al rebobinar (se silencia).
  - **Opciones del juego, el mecanismo** (2026-11-17): `games/<id>/options.c` con `game_opts[]` y `game_frame()` (`-DRT_GAME_OPTS`, lo pone `gba-port`); categoría **Extras** del menú, `config.ini` (`opt_<id>`) y `--game-opt id=valor`; probado con una opción de prueba (valor desde `config.ini`, pisado por la línea de órdenes, recortado al rango, aviso si no existe). Ver `docs/opciones-del-juego.md`. Falta: ganchos dentro de funciones del código generado, y las opciones concretas de *Minish Cap* (ver el plan del juego).
  - `GameOpt` admite nombres de valores (`values`, `values_es`: «Off,Hold B,Always»). `run_headless` acepta `--game-opt id=valor` (antes tomaba el valor como número de fotogramas y no terminaba).
  - Vídeo: «Barras laterales» (`borders=` en config.ini, activado por defecto): en una ventana más ancha que 3:2 rellena los lados con franjas en los colores del tema y un marco dorado alrededor de la imagen (`draw_bars` en `play_menu.h`). Comprobado en pantalla completa.
  - **Prueba de temporización** (`run_headless` imprime al final `main loop (VBlankIntrWait): ... frames between two: 1:N 2:M`): cuántos fotogramas pasan entre dos esperas de VBlank del juego. Todo debe estar en `1` (60 fps); un `2` repetido es un fallo de la temporización, no del juego. Así se encontró (2026-11-17) que `Halt` no despertaba tras ejecutar la interrupción y el juego iba a 30 fps (saltos, música lenta); tras arreglarlo, en 64 000 fotogramas solo hay unos pocos `2` (cambios de escena). También se corrigió el orden de las interrupciones VCOUNT (antes de dibujar la línea, sin que pase el tiempo dentro del manejador): causaba una raya parpadeante en la portada.
  - Siguiente: rebobinado probado a mano; selector de ranuras con miniaturas
  - **Ideas posibles** (no hechas): ratón y táctil (hover, clic en filas, sliders arrastrables); marca de «distinto del valor por defecto» en cada fila y restaurar solo esa fila con una tecla; pista de botones al pie según se use teclado o mando; animación corta de entrada y salida; avisos breves sin abrir el menú («Estado guardado», «Idioma cambiado»); opción de dejar el juego corriendo con el menú abierto; fuente más fina o vectorial en vez de 5×7 gruesa.

## 4. Herramientas, pruebas y empaquetado

- [ ] **Línea de órdenes**: `gba-port` como `snes-port` (construye desde la ROM del usuario, caché por CRC32, lanza), y un lanzador de ventana.
- [ ] **Ejecución sin ventana** (`headless`) con `--press`, `--poke`, `--screenshot` y `--dump`, igual que en snes-recomp, para probar sin mirar.
- [ ] **`scan`** equivalente al de snes-recomp: falla si lo que se sube lleva bytes de la ROM; usar antes de cada push (hoy los repos solo se apoyan en `.gitignore`).
- [ ] **`check.ps1` / `check.sh`** en el repositorio del juego.
- [ ] **Paquetes**: Windows, Linux (CI) y APK, con el mismo esquema de versiones y `SHA256SUMS`; actualizador del motor en el repositorio del juego (`update-engine.ps1`).
- [ ] **Compilador del lado del usuario**: TinyCC en Windows y en el móvil, gcc/clang en Linux, como en alttp-recomp.
- [ ] **Documentación**: README, CONTRIBUTING, un `docs\` con el formato del perfil y el diseño del runtime.
- [ ] **Licencia y legal**: MIT; no se copia código de `mstan/gbarecomp`, `999sian/tmc` ni `zeldaret/tmc` (sin licencia clara); de la decompilación, como mucho nombres de símbolos con cuidado. Nunca la ROM ni la BIOS oficial en los repos. Pasar los repos a públicos cuando haya algo que enseñar y el `scan` esté listo.

## 5. Juego: The Minish Cap

Detalle en `..\minishcap-recomp\plan.md`. Resumen:

- [ ] Que el motor lea el perfil; completar los *overlays* y las tablas conocidas.
- [ ] Llegar a ver el título, luego una partida jugable.
- [ ] Mejoras de calidad de vida (apagadas por defecto, sin romper la dificultad), pantalla ancha, traducción propia al español del texto, menú del port con estilo propio.
- [ ] Probar con el mando y con el móvil (Galaxy S25 Ultra por USB, como con alttp).

## Orden propuesto

1. Validar candidatos y punteros ARM; mapa de la ROM (cobertura). *(analizador)*
2. Spike de generación de C con una función pequeña y una prueba contra un emulador de referencia. *(decide la forma del C)*
3. Runtime mínimo: memoria, E/S, BIOS HLE básica, PPU de modo 0 y entrada hasta el título, sin sonido.
4. Despacho dinámico y respaldo (intérprete).
5. Sonido (M4A + FIFO + PSG) y guardado EEPROM.
6. Estados guardados, lanzador, paquetes, `scan`, `check`.
7. Después: calidad de vida, pantalla ancha, español, móvil.

## Riesgos conocidos

- El descubrimiento estático de *Minish Cap* deja 15 saltos dinámicos y punteros que no vemos; el respaldo en ejecución es obligatorio, no un lujo.
- La PPU y el tiempo (VBlank/HBlank/DMA) determinan si el juego arranca; dejar tiempo para depurarlo con comparación contra un emulador.
- Código en IWRAM/EWRAM que cambia en ejecución, si existiera, obligaría a un intérprete o a JIT.
- Tamaño del C generado (≈260 000 instrucciones): compilar en el móvil con TinyCC puede ser lento; medir pronto.

## Punto de reanudación (2026-11-15)

**Estado:** la intro de la vidriera termina y el juego entra en el mundo (se ve a Link en el exterior en el fotograma ~6400). Partida nueva creada con pulsaciones (`start@1600`, `a@1900`, `a@2300`, `start@2400`, `a@2650`, `a@2950`, `a@3300`, `a@3500`; el `.sav` resultante se guarda aparte).

**Causa del desvío de `sp` (cerrada):** un `bx rN` cuyo valor es la dirección de retorno de la función (tras `push {r2,lr}` ... `pop {r2,r3}` ... `bx r3`, en `0x0800429E`) se emitía como `rt_dispatch`, que ejecutaba la continuación del llamador y luego volvía a ejecutarla (+4 en `sp` por llamada). Ahora cada función guarda `lr0 = r14` al entrar y el salto indirecto hace `if (rN == lr0) return;` antes de despachar (`src/gen.rs`).

**Segundo desvío (cerrado):** la rutina de entidades de la IWRAM (`0x03005F24`, resume del bucle de `0x03005F40`) restaura el `sp` guardado del bucle (`ldr sp,[r11,#0xC]`) y sigue el bucle: es un `longjmp` que descarta los marcos de las funciones intermedias (+64 bytes). Sin tratarlo, `IntrMain` restauraba `IE=D3EF` y el juego se quedaba parado a los ~7270 fotogramas al andar. Ahora, tras cada llamada que devuelve con otro `sp`, si `r13 >= sp` de entrada de la función, esa función también retorna (`RT_UNWIND` en `runtime/gba_rt.h`, siempre activo). `rt_sp_mismatch` sigue listando esos 33 `longjmp` (esperado).

**Estado ahora:** 9000 fotogramas con partida nueva: intro, mundo y andar, sin congelarse (`IE=2005`).

**Siguiente:** jugar más tiempo y a mano (`tools/build_play.sh`) para hallar raíces dinámicas y fallos de PPU; después perfil `.toml`, sonido y estados guardados.

**Herramientas de diagnóstico ya hechas:**
- `-DRT_TRACE` al compilar el C generado: `RT_CHK_BEFORE/AFTER` comprueban que cada llamada devuelve con el mismo `sp`; `rt_sp_mismatch` (en un `trace.c` propio, ver abajo) agrupa por sitio de llamada/destino/variación. `RT_ENTER` da la traza de entradas de función. Sin `-DRT_TRACE` los macros no generan código.
- `YIELD_AT(addr)` anota el último bucle que cedió el control; `run_headless` imprime `last site`.
- `run_headless --dump-at FRAME` vuelca registros de vídeo y `dump_*.bin` (VRAM, paleta, OAM, E/S).
- `rt_sp_mismatch` y `rt_enter` los define quien compila con `-DRT_TRACE`; están en `tools/rt_trace.c` (agrupa por sitio, destino y variación, con el primer y último fotograma). Uso: `tcc -DRT_TRACE -Iruntime gen.c tools/rt_trace.c runtime/*.c ...`.

**Hecho hoy:** guardado EEPROM (`runtime/gba_save.c`); lectura de mapas de fondo con base >= `0x8000` en la PPU (la máscara `0x17FFF` quitaba el bit 15); lecturas directas de la CPU en `0x0D000000`; normalización de espejos de la ROM (`0x0A..`, `0x0C..`) en raíces y `rt_dispatch`.

**Pendientes (por orden):**
1. ~~Cerrar el desvío de `sp`~~ (hecho).
2. ~~Perfil `games/<id>.toml`~~ (hecho: `--profile`, `src/profile.rs`; lee `[[overlay]]` y `[roots] file`; el perfil de Minish Cap tiene las 41 raíces).
3. Sonido: hecho el motor (`runtime/gba_apu.c`: FIFO A/B por temporizador + DMA 1/2, PSG de 4 canales, 32768 Hz estéreo; SDL en `play.c`, `--wav` en `run_headless`). Con Minish Cap suena música (pico 14 764, pasajes de silencio coherentes con el juego). **Sin oír todavía**: falta que alguien lo escuche (tono, ritmo, clics) y afinar mezcla/volumen; el PSG no se ha ejercitado (el juego solo usa DS A). Posible mejora: resamplear mejor que el mantenimiento de orden cero.
4. ~~Estados guardados~~ (hecho). BIOS: revisada contra las 12 llamadas que usa Minish Cap (0,1,3,5,6,8,B,E,F,11,12,19); `runtime/bios_test.c` (59 comprobaciones: Div, Sqrt, ArcTan2, Obj/BgAffineSet, CpuSet/FastSet, LZ77, RL, BitUnPack, Huffman; compilar como dice su cabecera). `SoftReset` reinicia el juego (setjmp en los mains), `SoundBias` fija el sesgo, nuevas `BitUnPack` y `HuffUnComp`. Falta de la BIOS: `ArcTan` (aproximación, no el polinomio del BIOS), sonido por SWI 0x1A-0x1F, MultiBoot.
5. ~~`scan`~~ (hecho: `tools/scan.py`, hook con `tools/install_hooks.sh`; limpio sobre todo el historial).
6. ~~Limpiar los `.ppm` y `dump_*.bin` de la raíz~~ (no había nada; están en `.gitignore`).
7. **Pendiente, a retomar (lo hace el usuario a mano; no se puede comprobar desde aquí):**
   - Escuchar el sonido con `play.exe` (tono, ritmo, clics, volumen) y ajustar la mezcla en `runtime/gba_apu.c` si hace falta; el remuestreo es de orden cero y es lo primero a mejorar si suena áspero.
   - Probar F5/F8/1-9 (estados guardados) en la ventana.
   - Jugar más tiempo con la ventana para hallar raíces dinámicas nuevas (`tools/complete.sh`; actualizar `..\minishcap-recomp\games\dynamic_roots.txt`) y fallos de PPU (p. ej. la línea fina horizontal a la izquierda en el fotograma ~8500).
   - Idea: un único script que empaquete y construya `play.exe` para probar.
   - Commit en `..\minishcap-recomp` del perfil (`[roots]`, `[resume]`) y de `dynamic_roots.txt`, hoy sin subir.
   - Nada se ha subido (push) desde esta sesión; el hook `pre-push` hay que instalarlo en cada clon (`sh tools/install_hooks.sh`).
