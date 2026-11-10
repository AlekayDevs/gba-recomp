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

`src/discover.rs` reconoce también las tablas de `switch` de Thumb (`CMP Rx,#N` + `LDR Rt,=tabla` + `MOV PC,Rx`, mirando hacia atrás en el código porque el bloque puede empezar en el destino del `BLS`), `ADD Rd,PC,#imm` + `BX` (cambio a ARM/Thumb) y los espejos de la ROM (`0x0A000000`, `0x0C000000` se tratan como `0x08000000`). `src/rom.rs` tiene *overlays* (`--overlay RAM:ROM:LEN`): bloques que el arranque copia a RAM y se analizan con sus direcciones de RAM. Con *Minish Cap* USA y todo eso (`trace --pointers --overlay 030056F0:080B197C:135C --overlay 0300404C:080AF3A4:380`): 258 487 instrucciones (559 KB), 7088 funciones, 131 tablas de `switch` (1811 destinos) y 15 saltos indirectos sin resolver, que son dinámicos por naturaleza: los `__call_via_rN` (`bx r0..r12` en `0x08000EF2`), llamadas por tabla de punteros indexada (`ldr r1,[r2,r1]; bx r1`) y retornos tras `mov r12, lr`. El generador de C tendrá que emitir para ellos un despacho por dirección contra la tabla de funciones descubiertas (y un respaldo si no está).

Con *Minish Cap* USA: solo desde la entrada, 3998 instrucciones y 125 funciones; con `--pointers`, 246 773 instrucciones (533 KB), 7005 funciones y 142 saltos indirectos sin resolver, 4 «indefinidas» (falsos positivos en datos, regiones 0x084A.. a 0x0880..).

Hallazgos para lo que viene:
- El juego **copia funciones de la ROM a IWRAM** (código ARM en `0x030056F0`, `0x135C` bytes desde la ROM `0x080B197C`; el mezclador de sonido de M4A en `0x0300404C`, `0x380` bytes desde `0x080AF3A4`, copiado por `m4aSoundInit` en `0x080AFF48` con CpuSet; los dos ya cubiertos con `--overlay`) y las llama con `bx` desde pequeños trampolines en `0x0800029E`..: hay que saber de qué bloque de la ROM sale cada una (las copias las hace el arranque con CpuSet/DMA) y recompilarlas como código con dirección de RAM.
- `0x08000EF2`..`0x08000F00` son los `__call_via_rN` de libgcc (`bx r0`..`bx r12`): casi todas las llamadas por puntero pasan por ahí; el destino está en los datos.
- Pendiente del analizador: punteros a funciones ARM (pares), tablas de saltos de `switch`, validar los candidatos (decodificar un tramo entero válido) y desambiguar código y datos.

Siguiente: lo anterior, la generación de C y el runtime (memoria, E/S, PPU, BIOS).

## Reglas

- Nunca la ROM ni nada derivado (`*.gba`, `*.srm`, volcados, C generado, capturas con gráficos): están en `.gitignore`. Antes de cada push, comprobar que no se sube nada de la ROM (como el `scan` de snes-recomp; habrá que hacer uno equivalente).
- C/Rust original (clean-room). `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo se usan como referencia de ideas, no se copia su código.
- Lo del juego va en el repositorio del juego; aquí solo lo general (traductor, PPU, sonido, runtime).
- Documentación y mensajes de commit en español.
