# Opciones del juego

Un juego puede ofrecer ajustes propios (mejoras de calidad de vida, arreglos de fallos del original). Salen en la categoría **Extras** del menú F1 del reproductor, se guardan en `config.ini` (`opt_<id>=valor`) y se pueden dar en la línea de órdenes: `play <rom> --game-opt fast_text=1` (con `gba-port`: `gba-port -- --game-opt fast_text=1`).

## Cómo se escribe

En `games/<id>/options.c` (el nombre importa: `gba-port` lo reconoce y compila con `-DRT_GAME_OPTS`):

```c
#include "gba_sys.h"

const GameOpt game_opts[] = {
    /* id, nombre, nombre es, descripción, descripción es, defecto, mín, máx, [valores, valores es] */
    {"fast_text", "Fast text", "Texto rápido", "Text appears faster.", "El texto sale más rápido.", 0,       0,   1},
};
const int game_opt_count = 1;

void game_frame(void) {          /* una vez por fotograma, entre fotogramas */
    if (rt_opt("fast_text")) { /* leer o escribir memoria: rt_ewram, rt_iwram... */ }
}
```

- Un interruptor tiene `min 0` y `max 1` (el menú dice Sí/No); otro rango se muestra como número y se cambia con izquierda y derecha.
- `rt_opt("id")` da el valor actual; `game_frame` se llama justo antes del gancho de fotograma del reproductor y del ejecutable sin ventana, así que las pruebas con `run_headless --game-opt ...` ven lo mismo.
- Hasta 32 opciones. Los textos en español solo se usan si el juego está en español (`name_es`, `desc_es`; si están vacíos sale el inglés).
- Límite de este mecanismo: `game_frame` actúa sobre la memoria entre fotogramas. Para cambiar el comportamiento *dentro* de una función del juego hará falta un gancho en el código generado (todavía no hay).
- Para un selector con nombres, los dos últimos campos dan los nombres de los valores separados por comas: `{"fast_text", ..., 0, 0, 2, "Off,Hold B,Always", "No,Con B,Siempre"}`.
