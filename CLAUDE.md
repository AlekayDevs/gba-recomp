# gba-recomp

Motor de recompilación estática de GBA (ARM7TDMI). Repositorio hermano de `..\snes-recomp` (mismo estilo: motor general + un repositorio por juego con su perfil y su C propio) y del primer juego, `..\minishcap-recomp`.

## Comandos

```
cargo build --release
cargo test                                            # decodificadores ARM y Thumb
./target/release/gba-recomp info <rom.gba>            # cabecera, comprobaciones, CRC32
./target/release/gba-recomp disasm <rom.gba> <dir> [n] [thumb]
./target/release/gba-recomp trace <rom.gba> [entrada] # descubrimiento de código desde la entrada
./target/release/gba-recomp gen <rom.gba> <out.c> --pointers # C de las funciones Thumb (spike; ver tools/spike.sh)
./target/release/gba-recomp map <rom.gba> [--pointers] [--overlay ..] # mapa: código, pools, tablas y huecos
```

## Estado

El plan completo (pendiente, orden y riesgos) está en `plan.md`.

Decidido: motor propio y estático en Rust, sin dependencias por ahora (`docs/investigacion.md`). Hecho: `src/rom.rs` (cabecera, mapa de direcciones, CRC32), `src/arm.rs` y `src/thumb.rs` (decodificación: texto y `Flow`), `src/discover.rs` (sigue el flujo; resuelve `BX Rm` solo con un literal cargado justo antes; con `trace --pointers` añade como raíces los punteros Thumb impares de los datos que apuntan a un `PUSH {..,LR}`).

`src/discover.rs` reconoce también las tablas de `switch` de Thumb (`CMP Rx,#N` + `LDR Rt,=tabla` + `MOV PC,Rx`, mirando hacia atrás en el código porque el bloque puede empezar en el destino del `BLS`), `ADD Rd,PC,#imm` + `BX` (cambio a ARM/Thumb) y los espejos de la ROM (`0x0A000000`, `0x0C000000` se tratan como `0x08000000`). `src/rom.rs` tiene *overlays* (`--overlay RAM:ROM:LEN`): bloques que el arranque copia a RAM y se analizan con sus direcciones de RAM. Con *Minish Cap* USA y todo eso (`trace --pointers --overlay 030056F0:080B197C:135C --overlay 0300404C:080AF3A4:380`): 258 487 instrucciones (559 KB), 7088 funciones, 131 tablas de `switch` (1811 destinos) y 15 saltos indirectos sin resolver, que son dinámicos por naturaleza: los `__call_via_rN` (`bx r0..r12` en `0x08000EF2`), llamadas por tabla de punteros indexada (`ldr r1,[r2,r1]; bx r1`) y retornos tras `mov r12, lr`. El generador de C tendrá que emitir para ellos un despacho por dirección contra la tabla de funciones descubiertas (y un respaldo si no está).

Con *Minish Cap* USA: solo desde la entrada, 3998 instrucciones y 125 funciones; con `--pointers`, 246 773 instrucciones (533 KB), 7005 funciones y 142 saltos indirectos sin resolver, 4 «indefinidas» (falsos positivos en datos, regiones 0x084A.. a 0x0880..).

Hallazgos para lo que viene:
- El juego **copia funciones de la ROM a IWRAM** (código ARM en `0x030056F0`, `0x135C` bytes desde la ROM `0x080B197C`; el mezclador de sonido de M4A en `0x0300404C`, `0x380` bytes desde `0x080AF3A4`, copiado por `m4aSoundInit` en `0x080AFF48` con CpuSet; los dos ya cubiertos con `--overlay`) y las llama con `bx` desde pequeños trampolines en `0x0800029E`..: hay que saber de qué bloque de la ROM sale cada una (las copias las hace el arranque con CpuSet/DMA) y recompilarlas como código con dirección de RAM.
- `0x08000EF2`..`0x08000F00` son los `__call_via_rN` de libgcc (`bx r0`..`bx r12`): casi todas las llamadas por puntero pasan por ahí; el destino está en los datos.
- Pendiente del analizador: punteros a funciones ARM (pares), tablas de saltos de `switch`, validar los candidatos (decodificar un tramo entero válido) y desambiguar código y datos.

`discover_with` valida los candidatos de los datos (Thumb y punteros ARM a `STMFD ..,LR`): cada uno se recorre aparte y se descarta si llega a una indefinida o sale de la ROM. Con *Minish Cap* y `--pointers`: 258 976 instrucciones, 7095 funciones, 0 indefinidas, 7 candidatos descartados.

`map` (`src/map.rs`) da la cobertura: en el área de código de *Minish Cap* (`0x08000000..0x080B197A`, 727 KB) el 76,7 % es código, el 7,0 % pools y tablas, y quedan 108 KB de datos sin clasificar; a partir de `0x080B2A70` hay 16 MB de datos.

`gen` (`src/gen.rs`) traduce Thumb a C (spike): una función C por función, registros e indicadores como locales; 6782 de 7054 funciones, TinyCC compila los 14 MB en 1,4 s y `tools/spike_diff.py` no encuentra diferencias con Unicorn en 52 138 casos. Detalle y límites en `docs/generacion-c.md`. Falta: `switch`, saltos indirectos, ARM, `SWI`.

Siguiente: lo anterior, la generación de C y el runtime (memoria, E/S, PPU, BIOS).

## Reglas

- Nunca la ROM ni nada derivado (`*.gba`, `*.srm`, volcados, C generado, capturas con gráficos): están en `.gitignore`. Antes de cada push, comprobar que no se sube nada de la ROM (como el `scan` de snes-recomp; habrá que hacer uno equivalente).
- C/Rust original (clean-room). `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo se usan como referencia de ideas, no se copia su código.
- Lo del juego va en el repositorio del juego; aquí solo lo general (traductor, PPU, sonido, runtime).
- Documentación y mensajes de commit en español.
