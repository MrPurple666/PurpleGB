#ifndef PPU_H
#define PPU_H

#include "memory.h"
#include <SDL3/SDL.h>

#define LCD_WIDTH  160
#define LCD_HEIGHT 144

typedef struct {
    // LCD registers (cached for fast access)
    u8 lcdc, stat, scy, scx, ly, lyc, bgp, obp0, obp1, wy, wx;

    // Framebuffer (ARGB8888)
    u32 framebuffer[LCD_WIDTH * LCD_HEIGHT];

    // PPU state machine
    int mode;
    int cycles;

    // Previous STAT signal for edge detection
    int stat_signal;

    // Current scanline pixel buffer (color indices 0-3)
    u8 line_buf[LCD_WIDTH];

    // Window line counter
    u8 window_line;
} ppu_t;

void ppu_init(ppu_t *ppu);
void ppu_tick(ppu_t *ppu, mem_t *mem, int cycles);
void ppu_write_reg(ppu_t *ppu, u8 reg, u8 val);
u8   ppu_read_reg(ppu_t *ppu, u8 reg);

#endif
