# Investigación (2026-11-10): proyectos existentes de The Minish Cap y de recompilación de GBA

La ROM de partida: *The Legend of Zelda: The Minish Cap* (USA), SHA-1 `b4bd50e4131b027c334547b4524e2dbbd4227130`, 16 MiB, guardado en EEPROM.

## Lo que ya existe

| Proyecto | Qué es | Notas |
|---|---|---|
| `zeldaret/tmc` (937★) | **Decompilación** en C que reconstruye la ROM byte a byte (USA/JP/EU). | Base de casi todo lo demás. Sin licencia declarada; el código del juego es de Nintendo. Útil como mapa: nombres de funciones, estructuras y tablas. |
| `999sian/tmc`: **Project Picori** | **Port nativo para PC** a partir de la decompilación: SDL3, PPU por software propio (`port/ppu`), audio con agbplay. Linux, Windows y macOS. | Es el enfoque «decomp»: compila el C de la decompilación para x86 y sustituye el hardware. Multi-región por SHA-1; extrae los assets de la ROM del usuario al arrancar. |
| `EstebanPdN/zelda-tmc-3ds` (811★) | Port a 3DS sobre Picori (y un port Android de dos pantallas, `samyost1/tmc-android`). | Hecho con ayuda de Codex y Claude Code; muestra que el camino Picori escala a otras plataformas (también N64, Vita, RG34XX, Trimui...). |
| `mstan/MinishCapRecomp` | **Recompilación estática** de la ROM (ARM a C++) sobre el framework `mstan/gbarecomp`. | Solo toma de la decompilación los *nombres de símbolos* (no el C). Arranca BIOS, título y partida; el código que la estática no cubre corre en un intérprete, se compila a nativo (JIT) y se guarda en disco. Se recompila también la BIOS de GBA. |
| `mstan/gbarecomp` (74★) | Framework: analizador ARM/Thumb a C++, runtime de hardware (PPU, audio, DMA, timers, EEPROM...), widescreen adaptativo, netplay, estados, Android. | Otros juegos sobre él: Pokémon Esmeralda/Rojo Fuego/Rubí, Dragon Ball Z, Mario Kart Super Circuit, Mega Man Zero. Comunidad «R.A.I.D.» (Discord). Licencia `NOASSERTION`: revisar antes de reutilizar nada. |
| `sp00nznet/gbarecomp`, `Asphaltian/GBARecomp` | Otros recompiladores de GBA, más pequeños. | Solo referencia. |

## Tres caminos

1. **Motor propio, estático (como snes-recomp)**: traductor ARM/Thumb a C en Rust + runtime propio. Más trabajo, pero es coherente con snes-recomp, todo es nuestro (clean-room, MIT) y sirve para más juegos de GBA. Riesgos: saltos indirectos, mezcla ARM/Thumb, código en RAM (IWRAM/EWRAM) y la PPU y el sonido de GBA (el sonido del juego es el motor M4A, que corre como código de la ROM).
2. **Apoyarse en `mstan/gbarecomp`**: ya llega a jugar Minish Cap. Poco trabajo para ver algo, pero dependemos de un proyecto sin licencia clara y no es «nuestro»; habría que contribuir allí o hacer un fork.
3. **Port a partir de la decompilación (Picori)**: el más avanzado para jugar de verdad (PC y consolas), pero no es recompilación: compila el C reconstruido de `zeldaret/tmc`, con su licencia y sus assets. Ya lo hacen otros: lo más útil aquí sería estudiarlo o contribuir.

## Recomendación inicial

Seguir el camino 1 en pequeño, para no repetir lo que ya hace Picori: empezar con un **spike** (como `spikes/` de snes-recomp) que lea la ROM, desensamble ARM/Thumb desde el punto de entrada y recompile un tramo (arranque, bucle principal y VBlank) con una PPU mínima, y comparar con los otros proyectos. Mirar mientras tanto cómo resuelve `mstan/gbarecomp` el análisis y la PPU, solo como ideas. Decidir tras el spike si el motor propio compensa frente al camino 2.

## Diferencias con SNES a tener en cuenta

- CPU ARM7TDMI de 32 bits con dos juegos de instrucciones (ARM y Thumb; el cambio se hace con `BX`). Registros de 32 bits, ejecución condicional.
- Memoria: BIOS 16 KiB, EWRAM 256 KiB, IWRAM 32 KiB, I/O, paleta, VRAM 96 KiB, OAM, ROM hasta 32 MiB y guardado (aquí EEPROM). El código ejecutado desde IWRAM/EWRAM (típico del M4A) obliga a tratarlo como código dinámico.
- Gráficos: 240x160, modos 0 a 5, 4 fondos, 128 sprites, efectos (mosaico, mezcla, ventanas) y DMA en HBlank. Mucho código del juego depende del tiempo de las IRQ.
- Sonido: 4 canales PSG más 2 de DMA directo; el juego lleva su propio mezclador (M4A) en software.
- La BIOS: muchas llamadas SWI (descompresión LZ77/Huffman/RL, división, CpuSet...). `gbarecomp` recompila la BIOS real; la alternativa es implementarlas en el runtime (HLE).
