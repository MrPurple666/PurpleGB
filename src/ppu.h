#ifndef PPU_H
#define PPU_H

#include "memory.h"
#include <SDL3/SDL.h>

#define LCD_WIDTH  160
#define LCD_HEIGHT 144

typedef struct {
    u32 framebuffer[LCD_WIDTH * LCD_HEIGHT];
    int mode;
    int cycles;
    int stat_signal;
    u8 line_buf[LCD_WIDTH];
    u8 window_line;
    u8 ly;
} ppu_t;

void ppu_init(ppu_t *ppu);
void ppu_tick(ppu_t *ppu, mem_t *mem, int cycles);
void ppu_write_reg(ppu_t *ppu, u8 reg, u8 val);
u8   ppu_read_reg(ppu_t *ppu, u8 reg);

#endif
