# Generación de C: el spike (Thumb)

Resultado del spike del paso 2 del plan: una función C por función Thumb descubierta, comprobada contra Unicorn.

## Forma del C (decidido)

```c
void f_08008926(Cpu *c) {
    LOAD();                                   // r0..r14 y N Z C V pasan a locales
    r13 -= 16; wr32(r13 + 0, r4); ...         // push {r4, r5, r6, lr}
    r14 = 0x0800892Du; STORE(); f_08008942(c); RELOAD();   // bl
    r4 = rd32(r13 + 0); ... r13 += 16; STORE(); return;    // pop {r4, r5, r6, pc}
}
```

- **Registros y indicadores como locales**, volcados a `Cpu` solo en las llamadas y los retornos. El compilador los mantiene en registros del procesador y elimina los indicadores que nadie lee.
- **`BL` es una llamada C** y escribe en `r14` el valor real de la dirección de retorno (`|1`): lo que `push {lr}` deja en la pila es lo que dejaría el hardware. `BX LR` y `POP {.., PC}` vuelven a C sin mirar el valor sacado de la pila (supone que la función se llamó con `BL`).
- **Saltos locales con `goto`**. Un salto a la entrada de otra función (o caer en ella) es una llamada final: `STORE(); f(c); return;`.
- Las instrucciones se emiten por dirección: si hay código de la función antes de la entrada (pasa) se salta con `goto` a ella.
- **Indicadores**: `ADDS/SUBS/ADCS/SBCS/CMP/CMN` y los desplazamientos (`lsl_c`, `lsr_c`, `asr_c`, `ror_c`) siguen la semántica de ARM, incluido el acarreo del desplazamiento y los casos de 0 y de 32 o más. Los indicadores se vuelcan en `Cpu` en cada llamada y retorno.
- **Memoria**: `rd8/rd16/rd32/rdsh/wr8/wr16/wr32` del runtime, con la rotación de los accesos sin alinear del ARM7TDMI. El spike usa matrices planas (ROM, EWRAM, IWRAM); en el runtime real las de EWRAM/IWRAM se inlinean y las de E/S pasan por funciones con efectos.
- Las constantes de los pools de literales se resuelven al generar (`ldr r1, [pc, #..]` es `r1 = 0x03003F80u;`).

## Resultado con *Minish Cap* USA

- Se traducen **6782 de 7054** funciones Thumb (96 %). Las 272 restantes: 256 con salto indirecto (las tablas de `switch` aún no) y 16 con `SWI` (BIOS).
- 14 MB de C. **TinyCC lo compila en 1,4 s** (`tcc -shared -rdynamic`) en el PC: el riesgo de tiempo de compilación del plan queda mucho más bajo (en el móvil habrá que medirlo).
- Prueba diferencial contra **Unicorn** (un núcleo ARM real) con `tools/spike_diff.py`: registros, `N Z C V` y todos los bytes de EWRAM e IWRAM. 0 diferencias: con 30 casos por función y semilla 7, 3100 funciones con algún caso válido y 52 138 casos válidos (137 506 descartados por fallo de acceso, 12 329 por llamar a una función sin traducir, 1476 que no terminan y 11 sin alinear).

## Cómo repetirlo

```
cargo build --release
TCC=<tcc> PY=<python con unicorn> tools/spike.sh <rom.gba> <directorio de trabajo> [casos] [semilla]
```

`gen` escribe el C y la lista de funciones; `runtime/gba_rt.{h,c}` es el runtime del spike. El C generado no se sube (deriva de la ROM).

## Límites de la prueba

- Unicorn emula un núcleo más moderno: no rota los accesos sin alinear, así que esos casos se descartan (la rotación está en el runtime pero sin contrastar). Tampoco coincide el acarreo tras `MUL` (indefinido en ARMv4).
- Muchos casos se descartan por fallo de acceso (los registros aleatorios apuntan a cualquier sitio) o por no terminar. Hay más cobertura con más casos o con entradas guiadas.
- Compara funciones enteras, incluidas las llamadas que traducimos. No cubre E/S, interrupciones ni BIOS.

## Qué falta del traductor

- `switch` (tablas, ya reconocidas por el analizador), saltos indirectos y `bx rN` por el despacho dinámico con respaldo.
- ARM: ejecución condicional, `LDM/STM`, `MSR/MRS`, multiplicaciones largas, `SWP`.
- `SWI` a la BIOS (HLE), E/S y los efectos de memoria raros.
- El código de IWRAM (*overlays*) con direcciones de RAM.
