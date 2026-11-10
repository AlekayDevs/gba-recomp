# Plan de gba-recomp

Estado a 2026-11-10. El motor es propio y estático (Rust), hermano de `snes-recomp`; el primer juego es *The Legend of Zelda: The Minish Cap* (USA), en `..\minishcap-recomp`. Este plan lleva lo del motor; lo específico del juego está en `..\minishcap-recomp\plan.md`. Investigación y alternativas: `docs\investigacion.md`.

## Hecho

- Lector de ROM: cabecera, comprobaciones, CRC32, espejos (`0x08/0x0A/0x0C`) y *overlays* (bloques que el arranque copia a RAM).
- Decodificadores ARM y Thumb: texto y flujo de control (`Flow`); 4 pruebas.
- Descubrimiento de código desde la entrada: `BX` con literal, `ADD Rd,PC` + `BX`, punteros Thumb de los datos, tablas de `switch` de Thumb.
- Con *Minish Cap* USA: 258 487 instrucciones (559 KB), 7088 funciones, 131 tablas de `switch`, 15 saltos indirectos dinámicos, 7 «indefinidas» (falsos positivos).
- Repositorios en GitHub (privados): `AlexPerez7/gba-recomp` y `AlexPerez7/minishcap-recomp`.

## 1. Analizador (lo que falta del descubrimiento)

- [ ] **Validar los candidatos a función**: ahora un puntero impar a un `PUSH {..,LR}` basta. Hay 7 «indefinidas» en datos (`0x084A717A`, `0x087F60F8`, `0x088087A6`, `0x0880DD78`...). Descartar la función entera si su recorrido llega a una instrucción indefinida o a datos; mirar también la longitud y que acabe en un retorno.
- [ ] **Punteros a funciones ARM** (direcciones pares): hoy solo se buscan Thumb. Sin ellos el código ARM de la ROM (el de IWRAM se ve por los *overlays*) queda corto: 341 instrucciones ARM en total.
- [ ] **Separar código y datos**: los pools de literales y las tablas dentro del código se detectan por no alcanzarse; hay que marcarlos para no volver a tratarlos como código al recorrer linealmente y para no emitirlos.
- [ ] **Resolver más saltos**: `ldr rX,[rBase,rIndex]; bx rX` con una tabla de punteros (hay 2 en `0x0800129A` y `0x080012C4`); `bx r12` tras `mov r12, lr` (retorno, no salto); no confundir los `__call_via_rN` de libgcc (`0x08000EF2`..`0x08000F00`).
- [ ] **Tablas de `switch` de ARM** (el analizador solo reconoce las de Thumb) y las de otras formas (`TBB`-like no existen en ARMv4, pero hay variantes con `add pc, pc, r0, lsl #2`).
- [ ] **Más *overlays* automáticos**: hoy se pasan a mano (`--overlay`). Reconocer las rutinas de copia del arranque (`CpuSet`, `CpuFastSet`, DMA y bucles propios) y deducir origen, destino y tamaño; avisar si hay código que se ejecuta desde EWRAM/IWRAM sin overlay (el análisis lo ve como «fuera de la ROM»).
- [ ] **Código ejecutado desde RAM generado en ejecución** o que se modifica: confirmar que *Minish Cap* no lo hace (parece que solo copia bloques fijos).
- [ ] **Perfil del juego**: que el motor lea `games\<id>.toml` (hoy hay un borrador con los dos *overlays*): entradas, overlays, tablas de saltos conocidas, ganchos, nombres de símbolos propios.
- [ ] **Mapa de la ROM** (`map`): código, datos, tablas, gráficos comprimidos; para comprobar la cobertura y no dejar huecos.

## 2. Generación de C

Decisiones por tomar (hacer un *spike* con una función pequeña de punta a punta antes de decidir):

- [ ] **Forma del C**: una función C por función descubierta, con los registros ARM como variables locales (o una estructura `cpu`) y los indicadores N, Z, C, V explícitos y perezosos. Cuidar el rendimiento en Android (TinyCC en el móvil, como en alttp-recomp).
- [ ] **ARM y Thumb**: traducir las dos familias; ejecución condicional de ARM; los cambios de modo con `BX` son llamadas o saltos entre funciones de estilo distinto.
- [ ] **Saltos indirectos**: despacho por dirección contra una tabla de funciones y bloques descubiertos, con un **respaldo** (intérprete ARM/Thumb) si la dirección no está, como hace `mstan/gbarecomp`; contar y registrar los fallos para ir ampliando el análisis.
- [ ] **Memoria**: los accesos a ROM, EWRAM, IWRAM, E/S, paleta, VRAM y OAM pasan por funciones de lectura/escritura del runtime (los de E/S y VRAM tienen efectos); los de EWRAM/IWRAM conviene inlinearlos.
- [ ] **Instrucciones con efectos de memoria raros**: `SWP`, `LDM/STM` con `^`, `LDR` sin alinear (rotación), `STR` de PC, accesos de byte a VRAM (se ignoran en parte), `MSR`/`MRS` y los cambios de modo del procesador (IRQ, SVC, SYS: bancos de registros).
- [ ] **Tiempo**: la GBA no tiene un contador de ciclos observable, pero el juego depende de VBlank/HBlank, temporizadores y DMA; decidir si se cuentan los ciclos (aproximados) o se avanza por eventos.
- [ ] **Una prueba contra el original**: ejecutar el mismo tramo en un emulador de referencia (mGBA/VBA) y comparar registros y memoria en puntos fijos; el equivalente del `check` de alttp-recomp.

## 3. Runtime

En orden de aparición para llegar al título y a la partida:

- [ ] **Mapa de memoria** y espejos; BIOS de 16 KiB vacía o propia (no se redistribuye la oficial).
- [ ] **BIOS**: decidir entre HLE (propia, en el runtime) o recompilar la BIOS real, que no se puede repartir; la HLE es lo razonable. Llamadas que usa *Minish Cap* (SWI): `0` SoftReset, `1` RegisterRamReset, `3` Stop?, `5` VBlankIntrWait, `6` Div, `8` Sqrt, `11` CpuSet, `12` CpuFastSet, `14` BgAffineSet?, `15` ObjAffineSet, `17`/`18` descompresión LZ77, `25` SoundBias/SoundDriver..., y las de descompresión Huffman/RL si aparecen. Comprobar cada número con el desensamblado.
- [ ] **Interrupciones**: IME/IE/IF, VBlank, HBlank, temporizadores, DMA, teclado, serie; el manejador de la ROM se llama al entrar en la IRQ (la BIOS real lo llama desde `0x03007FFC`).
- [ ] **DMA** 0 a 3 (inmediato, VBlank, HBlank, sonido FIFO).
- [ ] **Temporizadores** 0 a 3 (el sonido directo los usa para el ritmo de muestreo).
- [ ] **PPU**: modos 0 a 2 (el juego usa sobre todo el 0), fondos con scroll, afines, sprites (normales, afines, doble tamaño), prioridades, mosaico, mezcla (alfa y brillo), ventanas, paletas, 15 bits de color. Primero por líneas, con efectos por línea (HBlank DMA/IRQ).
- [ ] **Sonido**: el juego lleva su propio motor M4A (código en la ROM y en IWRAM) que escribe en los registros de DMA directo A/B y en los 4 canales PSG; el runtime solo debe emular el hardware (FIFO, temporizadores, PSG, mezcla a 32768 Hz / 48 kHz).
- [ ] **Guardado**: EEPROM de 512 B o 8 KiB (se detecta por los accesos DMA); `.sav` junto al ejecutable (como `saves\` en alttp-recomp).
- [ ] **Entrada**: teclado y mando (reutilizar el mapeo de snes-recomp, que ya trae mando, táctil y Android); `KEYINPUT`, `KEYCNT`.
- [ ] **Ventana, vídeo y audio**: SDL2 como en snes-recomp; escalado entero, pantalla ancha después.
- [ ] **Estados guardados** y rebobinado: definir el punto de reanudación (como `[resume]` en alttp) y qué estado se guarda (registros de la CPU, EWRAM, IWRAM, VRAM, paleta, OAM, E/S, temporizadores, DMA, estado del sonido).

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
