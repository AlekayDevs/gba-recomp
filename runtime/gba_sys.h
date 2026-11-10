/* Internal interface of the runtime: the memory of the console, the I/O registers and what happens around the CPU (frames, interrupts, DMA, timers). */
#ifndef GBA_SYS_H
#define GBA_SYS_H
#include "gba_rt.h"

#define IO_DISPCNT 0x000
#define IO_DISPSTAT 0x004
#define IO_VCOUNT 0x006
#define IO_SOUNDBIAS 0x088
#define IO_DMA0 0x0B0 /* 12 bytes per channel: SAD, DAD, CNT_L, CNT_H */
#define IO_TM0 0x100  /* 4 bytes per timer: counter / reload, control */
#define IO_KEYINPUT 0x130
#define IO_KEYCNT 0x132
#define IO_IE 0x200
#define IO_IF 0x202
#define IO_WAITCNT 0x204
#define IO_IME 0x208

extern uint8_t rt_ewram[0x40000], rt_iwram[0x8000], rt_pal[0x400], rt_vram[0x18000], rt_oam[0x400], rt_io[0x400];
extern uint8_t *rt_rom;
extern u32 rt_rom_len;

u32 io_get16(u32 off);
void io_set16(u32 off, u32 v); /* storage only, no side effects */

/* system.c */
void sys_init(Cpu *c);
void sys_frame(Cpu *c);            /* one frame: 228 lines, VBlank, interrupts, DMA, timers */
void sys_raise(u32 irq_mask);      /* IF |= mask */
void sys_dma_enable(int ch);       /* DMAxCNT_H written with the enable bit */
void sys_timer_control(int t, u32 old, u32 nw);
u32 sys_timer_read(int t);
void sys_poll(void);
void sys_halt(Cpu *c);             /* wait for an interrupt: advance a frame */
void sys_run_irq(Cpu *c);
extern u32 sys_frames;
extern u32 sys_max_frames;
extern void (*sys_frame_hook)(u32 frame);
int ppu_write_ppm(const char *path);
extern uint32_t ppu_frame[160][240];
extern u32 sys_keys; /* pressed keys, active high (bit 0 = A) */

/* bios.c */
void bios_swi(Cpu *c, u32 n);
#endif
