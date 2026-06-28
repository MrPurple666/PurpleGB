#include <stdio.h>
#include "ppu.h"
#include "interrupt.h"
#include <string.h>

// DMG grayscale palette colors (ARGB8888)
static const u32 dmg_colors[4] = {
    0xFFFFFFFF,  // white (0)
    0xFFAAAAAA,  // light gray (1)
    0xFF555555,  // dark gray (2)
    0xFF000000   // black (3)
};

// LCDC bit masks
#define LCDC_BG_DISPLAY      (1 << 0)
#define LCDC_OBJ_DISPLAY     (1 << 1)
#define LCDC_OBJ_SIZE        (1 << 2)  // 0=8x8, 1=8x16
#define LCDC_BG_MAP          (1 << 3)  // 0=9800, 1=9C00
#define LCDC_TILE_DATA       (1 << 4)  // 0=8800(signed), 1=8000(unsigned)
#define LCDC_WIN_DISPLAY     (1 << 5)
#define LCDC_WIN_MAP         (1 << 6)  // 0=9800, 1=9C00
#define LCDC_LCD_ENABLE      (1 << 7)

// STAT bits
#define STAT_MODE_MASK       0x03
#define STAT_LYC_INTERRUPT   (1 << 6)
#define STAT_MODE2_INTERRUPT (1 << 5)
#define STAT_MODE1_INTERRUPT (1 << 4)
#define STAT_MODE0_INTERRUPT (1 << 3)
#define STAT_LYC_FLAG        (1 << 2)

// PPU modes
#define PPU_MODE_HBLANK  0
#define PPU_MODE_VBLANK  1
#define PPU_MODE_OAM     2
#define PPU_MODE_TRANSFER 3

// Timing (DMG)
#define CYCLES_PER_SCANLINE 456
#define CYCLES_MODE2        80
#define CYCLES_MODE3       172
#define CYCLES_MODE0       204
#define SCANLINES_VBLANK   10
#define SCANLINES_TOTAL    154

void ppu_init(ppu_t *ppu)
{
    memset(ppu, 0, sizeof(*ppu));
    ppu->mode = PPU_MODE_OAM;
}

static void fetch_tile_line(u8 *pixels, mem_t *mem, int tile_index, int line, bool signed_mode)
{
    u16 addr;
    if (signed_mode)
        addr = 0x9000 + (s8)tile_index * 16 + line * 2;
    else
        addr = 0x8000 + tile_index * 16 + line * 2;

    u8 byte0 = mem_read(mem, addr);
    u8 byte1 = mem_read(mem, addr + 1);
    for (int i = 0; i < 8; i++)
        pixels[i] = ((byte0 >> (7-i)) & 1) | (((byte1 >> (7-i)) & 1) << 1);
}

void render_scanline(ppu_t *ppu, mem_t *mem, int ly)
{
    u8 lcdc = mem->io[0x40];

    // Clear line buffer
    memset(ppu->line_buf, 0, LCD_WIDTH);

    if (!(lcdc & LCDC_LCD_ENABLE)) {
        // LCD disabled: all white
        for (int x = 0; x < LCD_WIDTH; x++) {
            ppu->framebuffer[ly * LCD_WIDTH + x] = 0xFFFFFFFF;
        }
        return;
    }

    bool tile_signed = !(lcdc & LCDC_TILE_DATA);
    u8 scy = mem->io[0x42], scx = mem->io[0x43];
    if (lcdc & LCDC_BG_DISPLAY) {
        u16 map_base = (lcdc & LCDC_BG_MAP) ? 0x9C00 : 0x9800;
        for (int x = 0; x < LCD_WIDTH; x++) {
            int map_x = ((x + scx) & 0xFF) / 8;
            int map_y = ((ly + scy) & 0xFF) / 8;
            int tile_x = (x + scx) & 7;
            int tile_y = (ly + scy) & 7;
            u16 map_addr = map_base + map_y * 32 + map_x;
            int tile_index = mem_read(mem, map_addr);
            u8 pixels[8];
            fetch_tile_line(pixels, mem, tile_index, tile_y, tile_signed);
            ppu->line_buf[x] = (mem->io[0x47] >> (pixels[tile_x] * 2)) & 3;
        }
    }

    // --- Window ---
    if ((lcdc & LCDC_WIN_DISPLAY) && ly >= mem->io[0x4A]) {
        u16 win_map = (lcdc & LCDC_WIN_MAP) ? 0x9C00 : 0x9800;
        int win_x = mem->io[0x4B] - 7;

        for (int x = 0; x < LCD_WIDTH; x++) {
            int screen_x = x;
            if (screen_x < win_x) continue;

            int map_x = (screen_x - win_x) / 8;
            int map_y = ppu->window_line / 8;
            int tile_x = (screen_x - win_x) & 7;
            int tile_y = ppu->window_line & 7;

            u16 map_addr = win_map + map_y * 32 + map_x;
            int tile_index = mem_read(mem, map_addr);

            u8 pixels[8];
            fetch_tile_line(pixels, mem, tile_index, tile_y, tile_signed);

            ppu->line_buf[screen_x] = (mem->io[0x47] >> (pixels[tile_x] * 2)) & 3;
        }
        ppu->window_line++;
    }

    // --- Sprites ---
    if (lcdc & LCDC_OBJ_DISPLAY) {
        int sprite_height = (lcdc & LCDC_OBJ_SIZE) ? 16 : 8;

        // Collect sprites on this scanline (max 10 per scanline)
        struct { u8 y, x, tile, attr; } sprites[10];
        int sprite_count = 0;

        for (int i = 0; i < 40 && sprite_count < 10; i++) {
            u8 sy = mem->oam[i * 4];
            u8 sx = mem->oam[i * 4 + 1];
            u8 stile = mem->oam[i * 4 + 2];
            u8 sattr = mem->oam[i * 4 + 3];

            int y_pos = sy - 16;
            if (ly >= y_pos && ly < y_pos + sprite_height) {
                sprites[sprite_count].y = sy;
                sprites[sprite_count].x = sx;
                sprites[sprite_count].tile = stile;
                sprites[sprite_count].attr = sattr;
                sprite_count++;
            }
        }

        // Render sprites (DMG priority: lower X higher priority)
        for (int s = 0; s < sprite_count; s++) {
            u8 sy = sprites[s].y;
            u8 sx = sprites[s].x;
            u8 stile = sprites[s].tile;
            u8 sattr = sprites[s].attr;

            bool x_flip = (sattr & 0x20) != 0;
            bool y_flip = (sattr & 0x40) != 0;
            bool bg_priority = (sattr & 0x80) != 0; // DMG: 1 = behind BG
            u8 palette = (sattr & 0x10) ? mem->io[0x49] : mem->io[0x48];

            int sprite_y = sy - 16;
            int tile_line = ly - sprite_y;
            if (y_flip) tile_line = sprite_height - 1 - tile_line;

            int tile_index = stile;
            if (sprite_height == 16) tile_index &= 0xFE; // 8x16: use bits 1-7 of tile index, bit 0 selects bank

            u8 pixels[8];
            fetch_tile_line(pixels, mem, tile_index, tile_line & 7, false);

            int x_start = sx - 8;
            for (int p = 0; p < 8; p++) {
                int px = x_start + (x_flip ? (7 - p) : p);
                if (px < 0 || px >= LCD_WIDTH) continue;

                u8 color = pixels[p];
                if (color == 0) continue; // transparent

                // Apply sprite palette
                u8 pal_color = (palette >> (color * 2)) & 3;

                // Priority: only draw if BG pixel is color 0 or no priority
                u8 bg_color = ppu->line_buf[px];
                if (!bg_priority || bg_color == 0) {
                    ppu->line_buf[px] = pal_color;
                }
            }
        }
    }

    for (int x = 0; x < LCD_WIDTH; x++)
        ppu->framebuffer[ly * LCD_WIDTH + x] = dmg_colors[ppu->line_buf[x]];
}
void ppu_render_frame(ppu_t *ppu, mem_t *mem)
{
    ppu->window_line = 0;
    for (int y = 0; y < LCD_HEIGHT; y++)
        render_scanline(ppu, mem, y);
}

// Update LY-LYC coincidence and STAT interrupt
static void update_stat(ppu_t *ppu, mem_t *mem)
{
    bool lyc_eq = (ppu->ly == mem->io[0x45]);
    u8 stat = mem->io[0x41];

    // Set LYC flag
    if (lyc_eq)
        stat |= STAT_LYC_FLAG;
    else
        stat &= ~STAT_LYC_FLAG;

    // Set mode bits
    stat = (stat & ~STAT_MODE_MASK) | (ppu->mode & STAT_MODE_MASK);
    mem->io[0x41] = stat;

    // Compute combined STAT signal
    int new_signal = 0;
    if (lyc_eq && (stat & STAT_LYC_INTERRUPT)) new_signal = 1;
    if (ppu->mode == 0 && (stat & STAT_MODE0_INTERRUPT)) new_signal = 1;
    if (ppu->mode == 1 && (stat & STAT_MODE1_INTERRUPT)) new_signal = 1;
    if (ppu->mode == 2 && (stat & STAT_MODE2_INTERRUPT)) new_signal = 1;

    // Trigger STAT interrupt on rising edge
    if (new_signal && !ppu->stat_signal) {
        interrupt_request(mem, INT_STAT);
    }
    ppu->stat_signal = new_signal;
}

void ppu_tick(ppu_t *ppu, mem_t *mem, int cycles)
{
    if (!(mem->io[0x40] & LCDC_LCD_ENABLE)) {
        // LCD disabled: LY stays 0, mode stays 0 (HBlank)
        ppu->ly = 0;
        ppu->mode = PPU_MODE_HBLANK;
        ppu->cycles = 0;
        // Clear framebuffer to white
        for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
            ppu->framebuffer[i] = 0xFFFFFFFF;
        mem->io[0x44] = 0; // LY
        return;
    }

    ppu->cycles += cycles;

    if (ppu->ly < LCD_HEIGHT) {
        // Visible scanlines
        if (ppu->cycles >= CYCLES_PER_SCANLINE) {
            ppu->cycles -= CYCLES_PER_SCANLINE;
            ppu->ly++;
            mem->io[0x44] = ppu->ly;

            if (ppu->ly == LCD_HEIGHT) {
                // Enter VBlank
                ppu->mode = PPU_MODE_VBLANK;
                update_stat(ppu, mem);
                interrupt_request(mem, INT_VBLANK);
                ppu->window_line = 0;
            } else {
                ppu->mode = PPU_MODE_OAM;
                update_stat(ppu, mem);
            }
        } else if (ppu->cycles < CYCLES_MODE2) {
            ppu->mode = PPU_MODE_OAM;
            update_stat(ppu, mem);
        } else if (ppu->cycles < CYCLES_MODE2 + CYCLES_MODE3) {
            ppu->mode = PPU_MODE_TRANSFER;
            update_stat(ppu, mem);
            if (ppu->cycles - cycles < CYCLES_MODE2 &&
                ppu->cycles >= CYCLES_MODE2) {
                // Started pixel transfer — render the scanline
                render_scanline(ppu, mem, ppu->ly);
            }
        } else {
            ppu->mode = PPU_MODE_HBLANK;
            update_stat(ppu, mem);
        }
    } else {
        // VBlank scanlines (LY 144-153)
        if (ppu->cycles >= CYCLES_PER_SCANLINE) {
            ppu->cycles -= CYCLES_PER_SCANLINE;
            ppu->ly++;
            mem->io[0x44] = ppu->ly;

            if (ppu->ly >= SCANLINES_TOTAL) {
                // Start new frame
                ppu->ly = 0;
                mem->io[0x44] = 0;
                ppu->mode = PPU_MODE_OAM;
                update_stat(ppu, mem);
            } else {
                // Still in VBlank
                ppu->mode = PPU_MODE_VBLANK;
                update_stat(ppu, mem);
            }
        }
    }
}
