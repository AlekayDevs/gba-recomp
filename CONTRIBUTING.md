# Contribuir

Este repositorio es el **motor** (traductor, runtime y `gba-port`); cada juego vive en su repositorio con su perfil (`games/<id>.toml`) y su C propio. El primero es `minishcap-recomp`.

## Reglas que no se negocian

- **Nunca ROMs ni la BIOS oficial.** Ni en commits, ni en issues, ni en pull requests, ni enlaces. `.gitignore` excluye `*.gba`, `*.sav`, `*.srm`, volcados y el C generado.
- **Nada derivado de la ROM.** C generado, volcados de memoria, capturas con gráficos del juego o tablas copiadas bytes a bytes no se suben. `python tools/scan.py --history` lo comprueba (lo ejecuta el hook `pre-push`: instálalo con `sh tools/install_hooks.sh`).
- **Clean-room.** Código propio. `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo sirven como referencia de ideas, no se copia su código. Si te apoyas en documentación pública del hardware, cítala en el comentario.
- **Lo del juego, en el repositorio del juego.** Aquí solo lo general.

## Compilar y probar

```
cargo build --release
cargo test        # decodificadores ARM y Thumb; no necesita ROM
```

Para probar con un juego real, usa el repositorio del juego (`gba-port <rom> --no-launch`) y `run_headless` del runtime (`--script`, `--shot-at`, `--rewind-test`, `--game-opt`).

## Qué tocar y dónde

| Quieres... | Mira |
|---|---|
| arreglar decodificación o descubrimiento | `src/arm.rs`, `src/thumb.rs`, `src/discover.rs` |
| cambiar el C generado | `src/gen.rs`, `docs/generacion-c.md` |
| vídeo, sonido, entrada o menú | `runtime/gba_ppu.c`, `runtime/gba_apu.c`, `runtime/play.c`, `runtime/play_menu.h` |
| cómo se construye el port o el lanzador | `src/port.rs`, `src/launcher*.rs` |

## Commits

Mensajes en español, en imperativo, con el qué y el porqué en la primera línea. Describe cómo lo probaste, sin adjuntar nada de la ROM.
