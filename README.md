# gba-recomp

Motor de recompilación estática para **Game Boy Advance** (CPU ARM7TDMI: ARM y Thumb), hermano de `snes-recomp`. Traduce la ROM del usuario a C y la enlaza con un runtime propio (PPU, sonido, DMA, temporizadores, guardado, entrada). No incrusta ningún juego: cada juego es un perfil en su propio repositorio (el primero, `minishcap-recomp`).

**Estado: solo la base.** Todavía no hay código. Falta decidir el enfoque (ver `docs/investigacion.md`).

La ROM nunca se sube ni se reparte: el usuario trae la suya.
