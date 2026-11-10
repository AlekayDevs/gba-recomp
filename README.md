# gba-recomp

Motor de recompilación estática para **Game Boy Advance** (CPU ARM7TDMI: ARM y Thumb), hermano de `snes-recomp`. Traduce la ROM del usuario a C y la enlaza con un runtime propio (PPU, sonido, DMA, temporizadores, guardado, entrada). No incrusta ningún juego: cada juego es un perfil en su propio repositorio (el primero, `minishcap-recomp`).

**Enfoque: motor propio y estático, como snes-recomp** (decidido el 2026-11-10; ver `docs/investigacion.md`).

**Estado: primer paso.** Hay un lector de ROM (cabecera y comprobaciones), un decodificador ARM y otro Thumb (texto y flujo de control) y un descubridor de código que sigue el flujo desde el punto de entrada. Todavía no genera C ni hay runtime.

```
cargo run --release -- info <rom.gba>                 # cabecera, comprobaciones, CRC32
cargo run --release -- disasm <rom.gba> 080000C0 24   # desensamblar (añade `thumb` para Thumb)
cargo run --release -- trace <rom.gba>                # seguir el flujo desde la entrada y resumir
cargo test
```

La ROM nunca se sube ni se reparte: el usuario trae la suya.
