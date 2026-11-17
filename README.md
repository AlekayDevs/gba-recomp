# gba-recomp

Motor de recompilación estática para **Game Boy Advance** (CPU ARM7TDMI: ARM y Thumb), hermano de `snes-recomp`. Traduce la ROM del usuario a C y la enlaza con un runtime propio (PPU, sonido, DMA, temporizadores, guardado, entrada, ventana SDL2). No incrusta ningún juego: cada juego es un perfil en su propio repositorio (el primero, [`minishcap-recomp`](../minishcap-recomp)).

**Estado: jugable en Windows.** *The Minish Cap* (USA) arranca, pasa la intro y se juega con sonido, guardado EEPROM, estados guardados, rebobinado, menú del reproductor (F1), opciones propias del juego y lanzador de ventana. No se ha jugado entero. El plan y lo pendiente están en `plan.md`; la decisión de diseño, en `docs/investigacion.md`.

## Piezas

| | |
|---|---|
| `src/rom.rs`, `arm.rs`, `thumb.rs`, `discover.rs`, `map.rs` | lector de ROM, decodificadores, descubrimiento de código y mapa de cobertura |
| `src/gen.rs` | traductor a C (ver `docs/generacion-c.md`) |
| `runtime/` | runtime en C: CPU/bus, E/S, DMA, PPU, sonido, BIOS HLE, estados, rebobinado, `play.c` (SDL2) y `play_menu.h` (menú) |
| `src/port.rs`, `src/bin/gba-port.rs`, `launcher*.rs` | `gba-port`: identifica la ROM por CRC32, genera y compila (TinyCC), cachea y lanza |
| `docs/opciones-del-juego.md` | cómo un juego añade sus opciones (`games/<id>/options.c`) |

## Órdenes

```
cargo build --release
cargo test
./target/release/gba-recomp info <rom.gba>            # cabecera, comprobaciones, CRC32
./target/release/gba-recomp trace <rom.gba> --pointers # descubrimiento de código
./target/release/gba-recomp map <rom.gba> --pointers   # cobertura: código, pools, tablas, huecos
./target/release/gba-port <rom.gba> --no-launch        # (en un repositorio de juego) construir sin lanzar
```

## Legal

La ROM nunca se sube ni se reparte: el usuario trae la suya. Nada derivado de ella (C generado, volcados, capturas) entra en el repositorio; `tools/scan.py` lo comprueba antes de cada push. Código propio, clean-room. Licencia MIT.
