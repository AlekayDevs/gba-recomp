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

Decidido: motor propio y estático en Rust, sin dependencias por ahora (`docs/investigacion.md`). Hecho: `src/rom.rs` (cabecera, mapa de direcciones, CRC32), `src/arm.rs` y `src/thumb.rs` (decodificación: texto y `Flow`), `src/discover.rs` (sigue el flujo; resuelve `BX Rm` solo con un literal cargado justo antes; con `trace --pointers` añade como raíces los punteros Thumb impares de los datos que apuntan a un `PUSH {..,LR}`).

Con *Minish Cap* USA: solo desde la entrada, 3998 instrucciones y 125 funciones; con `--pointers`, 246 773 instrucciones (533 KB), 7005 funciones y 142 saltos indirectos sin resolver, 4 «indefinidas» (falsos positivos en datos, regiones 0x084A.. a 0x0880..).

Hallazgos para lo que viene:
- El juego **copia funciones de la ROM a IWRAM** (`0x03005780`..`0x03005918`, el mezclador de sonido en `0x0300404C`, etc.) y las llama con `bx` desde pequeños trampolines en `0x0800029E`..: hay que saber de qué bloque de la ROM sale cada una (las copias las hace el arranque con CpuSet/DMA) y recompilarlas como código con dirección de RAM.
- `0x08000EF2`..`0x08000F00` son los `__call_via_rN` de libgcc (`bx r0`..`bx r12`): casi todas las llamadas por puntero pasan por ahí; el destino está en los datos.
- Pendiente del analizador: punteros a funciones ARM (pares), tablas de saltos de `switch`, validar los candidatos (decodificar un tramo entero válido) y desambiguar código y datos.

Siguiente: lo anterior, la generación de C y el runtime (memoria, E/S, PPU, BIOS).

## Reglas

- Nunca la ROM ni nada derivado (`*.gba`, `*.srm`, volcados, C generado, capturas con gráficos): están en `.gitignore`. Antes de cada push, comprobar que no se sube nada de la ROM (como el `scan` de snes-recomp; habrá que hacer uno equivalente).
- C/Rust original (clean-room). `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo se usan como referencia de ideas, no se copia su código.
- Lo del juego va en el repositorio del juego; aquí solo lo general (traductor, PPU, sonido, runtime).
- Documentación y mensajes de commit en español.
