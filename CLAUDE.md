# gba-recomp

Motor de recompilación estática de GBA (ARM7TDMI). Repositorio hermano de `..\snes-recomp` (mismo estilo: motor general + un repositorio por juego con su perfil y su C propio) y del primer juego, `..\minishcap-recomp`.

## Estado

Solo documentación. Antes de escribir código hay que decidir el enfoque con `docs/investigacion.md` (propio en Rust, o apoyarse en `mstan/gbarecomp`, o un port a partir de la decompilación).

## Reglas

- Nunca la ROM ni nada derivado (`*.gba`, `*.srm`, volcados, C generado, capturas con gráficos): están en `.gitignore`. Antes de cada push, comprobar que no se sube nada de la ROM (como el `scan` de snes-recomp; habrá que hacer uno equivalente).
- C/Rust original (clean-room). `mstan/gbarecomp`, `999sian/tmc` y `zeldaret/tmc` no declaran una licencia clara: solo se usan como referencia de ideas, no se copia su código.
- Lo del juego va en el repositorio del juego; aquí solo lo general (traductor, PPU, sonido, runtime).
- Documentación y mensajes de commit en español.
