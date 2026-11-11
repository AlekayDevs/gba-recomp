/* Diagnóstico de pila para el C generado: compilar con -DRT_TRACE junto a este fichero. Al terminar imprime los sitios de llamada cuyo callee devolvió con otro sp (sitio, destino, variación, primer y último fotograma). */
#include "gba_rt.h"
#include "gba_sys.h"
#include <stdio.h>
#include <stdlib.h>
void rt_enter(Cpu *c, u32 addr) { (void)c; (void)addr; }
#define MAXE 512
static struct { u32 site, target; int delta; u32 first, last, count; } ent[MAXE];
static int nent, registered;
static void report(void) {
    fprintf(stderr, "call sites whose callee returned with a different sp (site target delta first-frame last-frame count):\n");
    for (int i = 0; i < nent; i++) fprintf(stderr, "  %08X -> %08X  %+d  %u..%u  x%u\n", ent[i].site, ent[i].target, ent[i].delta, ent[i].first, ent[i].last, ent[i].count);
}
void rt_sp_mismatch(Cpu *c, u32 site, u32 target, u32 before, u32 after) {
    (void)c;
    if (!registered) { registered = 1; atexit(report); }
    int d = (int)(after - before);
    for (int i = 0; i < nent; i++) if (ent[i].site == site && ent[i].target == target && ent[i].delta == d) { ent[i].last = sys_frames; ent[i].count++; return; }
    if (nent < MAXE) { ent[nent].site = site; ent[nent].target = target; ent[nent].delta = d; ent[nent].first = ent[nent].last = sys_frames; ent[nent].count = 1; nent++; }
}
