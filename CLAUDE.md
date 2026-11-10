# gba-recomp

Motor de recompilación estática de GBA (ARM7TDMI). Repositorio hermano de `..\snes-recomp` (mismo estilo: motor general + un repositorio por juego con su perfil y su C propio) y del primer juego, `..\minishcap-recomp`.

## Comandos

```
cargo build --release
cargo test                                            # decodificadores ARM y Thumb
./target/release/gba-recomp info <rom.gba>            # cabecera, comprobaciones, CRC32
./target/release/gba-recomp disasm <rom.gba> <dir> [n] [thumb]
./target/release/gba-recomp trace <rom.gba> [entrada] # descubrimiento de código desde la entrada
```

## Estado

Decidido: motor propio y estático en Rust, sin dependencias por ahora (`docs/investigacion.md`). Hecho: `src/rom.rs` (cabecera, mapa de direcciones, CRC32), `src/arm.rs` y `src/thumb.rs` (decodificación: texto y `Flow`), `src/discover.rs` (sigue el flujo; resuelve `BX Rm` solo con un literal cargado justo antes). Con *Minish Cap* USA llega a 3998 instrucciones y 125 funciones desde `$080000C0`; el resto está tras punteros a funciones y tablas de saltos. Siguiente: resolver esos saltos indirectos (tablas, punteros en literales y en datos), la generación de C y el runtime (memoria, E/S, PPU, BIOS).

## Reglas

- Nunca la ROM ni nada derivado (`*.gba`, `*.srm`, volcados, C generado, capturas con gráficos): están en `.gitignore`. Antes de cada push, comprobar que no se sube nada de la ROM (como el `scan` de snes-recomp; habrá que hacer uno equivalente).
- C/Rust original (clean-room). `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo se usan como referencia de ideas, no se copia su código.
- Lo del juego va en el repositorio del juego; aquí solo lo general (traductor, PPU, sonido, runtime).
- Documentación y mensajes de commit en español.
