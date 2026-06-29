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

void ppu_decode_cgb_palettes(ppu_t *ppu, mem_t *mem)
{
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            int idx = i * 8 + j * 2;
            u16 c = mem->bg_palette[idx] | ((u16)mem->bg_palette[idx + 1] << 8);
            u8 r = (c & 0x1F) << 3;
            u8 g = ((c >> 5) & 0x1F) << 3;
            u8 b = ((c >> 10) & 0x1F) << 3;
            ppu->cgb_bg_palettes[i][j] = 0xFF000000 | ((u32)r << 16) | ((u32)g << 8) | b;

            c = mem->obj_palette[idx] | ((u16)mem->obj_palette[idx + 1] << 8);
            r = (c & 0x1F) << 3;
            g = ((c >> 5) & 0x1F) << 3;
            b = ((c >> 10) & 0x1F) << 3;
            ppu->cgb_obj_palettes[i][j] = 0xFF000000 | ((u32)r << 16) | ((u32)g << 8) | b;
        }
    }
}

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

/* Compute variable Mode 3 timing based on sprite count and SCX fine scroll */
static int compute_mode3_length(mem_t *mem, int ly) {
    u8 lcdc = mem->io[0x40];
    int mode3 = 172; /* base Mode 3 T-cycles */

    /* SCX fine scroll: bits 0-2 add 1-7 cycles */
    mode3 += mem->io[0x43] & 7;

    /* Sprite overhead: up to 10 sprites, each adds ~1-6 cycles depending on position */
    if (lcdc & LCDC_OBJ_DISPLAY) {
        int sprite_height = (lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
        int sprites_on_line = 0;
        for (int i = 0; i < 40 && sprites_on_line < 10; i++) {
            int sy = mem->oam[i * 4] - 16;
            if (ly >= sy && ly < sy + sprite_height) {
                sprites_on_line++;
            }
        }
        /* Rough approximation: 1-6 extra cycles per sprite */
        mode3 += sprites_on_line * 2;
    }

    /* Clamp to valid range */
    if (mode3 < 172) mode3 = 172;
    if (mode3 > 291) mode3 = 291; /* Max possible Mode 3 */
    return mode3;
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

    memset(ppu->line_buf, 0, LCD_WIDTH);
    memset(ppu->bg_color_buf, 0, LCD_WIDTH);

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

            if (mem->cgb) {
                /* Read attribute byte from VRAM bank 1 */
                u8 attr = mem->vram_banks[1][map_addr & 0x1FFF];
                u8 pal_num = attr & 0x07;
                bool vram_bank = (attr & 0x08) != 0;
                bool x_flip = (attr & 0x20) != 0;
                bool y_flip = (attr & 0x40) != 0;
                /* bool bg_priority = (attr & 0x80) != 0; */

                int ty = y_flip ? (7 - tile_y) : tile_y;
                /* Read tile data from specified VRAM bank */
                u16 tile_addr;
                if (tile_signed)
                    tile_addr = 0x9000 + (s8)tile_index * 16 + ty * 2;
                else
                    tile_addr = 0x8000 + tile_index * 16 + ty * 2;
                u8 byte0 = mem->vram_banks[vram_bank ? 1 : 0][tile_addr & 0x1FFF];
                u8 byte1 = mem->vram_banks[vram_bank ? 1 : 0][(tile_addr + 1) & 0x1FFF];
                for (int i = 0; i < 8; i++)
                    pixels[i] = ((byte0 >> (7-i)) & 1) | (((byte1 >> (7-i)) & 1) << 1);

                u8 color = pixels[x_flip ? (7 - tile_x) : tile_x];
                ppu->bg_color_buf[x] = color;
                ppu->line_buf[x] = pal_num * 4 + color;
            } else {
                fetch_tile_line(pixels, mem, tile_index, tile_y, tile_signed);
                u8 color = pixels[tile_x];
                ppu->bg_color_buf[x] = color;
                ppu->line_buf[x] = (mem->io[0x47] >> (color * 2)) & 3;
            }
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

            if (mem->cgb) {
                u8 attr = mem->vram_banks[1][map_addr & 0x1FFF];
                u8 pal_num = attr & 0x07;
                bool vram_bank = (attr & 0x08) != 0;
                bool x_flip = (attr & 0x20) != 0;
                bool y_flip = (attr & 0x40) != 0;

                int ty = y_flip ? (7 - tile_y) : tile_y;
                u16 tile_addr;
                if (tile_signed)
                    tile_addr = 0x9000 + (s8)tile_index * 16 + ty * 2;
                else
                    tile_addr = 0x8000 + tile_index * 16 + ty * 2;
                u8 byte0 = mem->vram_banks[vram_bank ? 1 : 0][tile_addr & 0x1FFF];
                u8 byte1 = mem->vram_banks[vram_bank ? 1 : 0][(tile_addr + 1) & 0x1FFF];
                for (int i = 0; i < 8; i++)
                    pixels[i] = ((byte0 >> (7-i)) & 1) | (((byte1 >> (7-i)) & 1) << 1);

                u8 color = pixels[x_flip ? (7 - tile_x) : tile_x];
                ppu->bg_color_buf[screen_x] = color;
                ppu->line_buf[screen_x] = pal_num * 4 + color;
            } else {
                fetch_tile_line(pixels, mem, tile_index, tile_y, tile_signed);
                u8 color = pixels[tile_x];
                ppu->bg_color_buf[screen_x] = color;
                ppu->line_buf[screen_x] = (mem->io[0x47] >> (color * 2)) & 3;
            }
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
            bool bg_priority = (sattr & 0x80) != 0;

            int sprite_y = sy - 16;
            int tile_line = ly - sprite_y;
            if (y_flip) tile_line = sprite_height - 1 - tile_line;

            int tile_index = stile;
            if (sprite_height == 16) tile_index &= 0xFE;

            u8 pixels[8];

            if (mem->cgb) {
                u8 pal_num = sattr & 0x07;
                bool vram_bank = (sattr & 0x08) != 0;

                /* Read tile data from specified VRAM bank */
                u16 tile_addr = 0x8000 + tile_index * 16 + (tile_line & 7) * 2;
                u8 byte0 = mem->vram_banks[vram_bank ? 1 : 0][tile_addr & 0x1FFF];
                u8 byte1 = mem->vram_banks[vram_bank ? 1 : 0][(tile_addr + 1) & 0x1FFF];
                for (int i = 0; i < 8; i++)
                    pixels[i] = ((byte0 >> (7-i)) & 1) | (((byte1 >> (7-i)) & 1) << 1);

                int x_start = sx - 8;
                for (int p = 0; p < 8; p++) {
                    int px = x_start + (x_flip ? (7 - p) : p);
                    if (px < 0 || px >= LCD_WIDTH) continue;

                    u8 color = pixels[p];
                    if (color == 0) continue; // transparent

                    u8 bg_col = ppu->line_buf[px] % 4;

                    /* CGB priority: if bg_priority set, BG wins unless BG color is 0 */
                    /* Also check BG tile attribute priority (bit 7 of BG attr) */
                    if (bg_priority && bg_col != 0) continue;

                    ppu->line_buf[px] = pal_num * 4 + color;
                }
            } else {
                fetch_tile_line(pixels, mem, tile_index, tile_line & 7, false);
                u8 palette = (sattr & 0x10) ? mem->io[0x49] : mem->io[0x48];

                int x_start = sx - 8;
                for (int p = 0; p < 8; p++) {
                    int px = x_start + (x_flip ? (7 - p) : p);
                    if (px < 0 || px >= LCD_WIDTH) continue;

                    u8 color = pixels[p];
                    if (color == 0) continue; // transparent

                    u8 pal_color = (palette >> (color * 2)) & 3;
                    u8 bg_color = ppu->bg_color_buf[px];
                    if (!bg_priority || bg_color == 0) {
                        ppu->line_buf[px] = pal_color;
                    }
                }
            }
        }
    }

    for (int x = 0; x < LCD_WIDTH; x++) {
        if (mem->cgb) {
            u8 val = ppu->line_buf[x];
            u8 pal = val / 4;
            u8 col = val % 4;
            ppu->framebuffer[ly * LCD_WIDTH + x] = ppu->cgb_bg_palettes[pal][col];
        } else {
            ppu->framebuffer[ly * LCD_WIDTH + x] = dmg_colors[ppu->line_buf[x]];
        }
    }
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
        if (ppu->lcd_was_on) {
            /* First tick after LCD off: reset once */
            ppu->ly = 0;
            ppu->mode = PPU_MODE_HBLANK;
            ppu->cycles = 0;
            ppu->window_line = 0;
            for (int i = 0; i < LCD_WIDTH * LCD_HEIGHT; i++)
                ppu->framebuffer[i] = 0xFFFFFFFF;
            mem->io[0x44] = 0;
            ppu->lcd_was_on = false;
            DBG(PPU, "LCD off  LY=0 mode=0");
        }
        return;
    }
    if (!ppu->lcd_was_on) {
        /* First tick after LCD on */
        ppu->lcd_was_on = true;
        ppu->cycles = 0;
        ppu->ly = 0;
        ppu->mode = PPU_MODE_OAM;
        mem->io[0x44] = 0;
        DBG(PPU, "LCD on  LY=0 mode=OAM");
    }

    ppu->cycles += cycles;

    if (ppu->ly < LCD_HEIGHT) {
        /* Visible scanlines */
        if (ppu->cycles >= CYCLES_PER_SCANLINE) {
            ppu->cycles -= CYCLES_PER_SCANLINE;
            ppu->mode3_length = compute_mode3_length(mem, ppu->ly);
            ppu->ly++;
            mem->io[0x44] = ppu->ly;
            if (ppu->ly == LCD_HEIGHT) {
                /* Enter VBlank */
                ppu->mode = PPU_MODE_VBLANK;
                DBG(PPU, "VBlank  LY=%d", ppu->ly);
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
        } else if (ppu->cycles < CYCLES_MODE2 + ppu->mode3_length) {
            ppu->mode = PPU_MODE_TRANSFER;
            update_stat(ppu, mem);
            if (ppu->cycles - cycles < CYCLES_MODE2 &&
                ppu->cycles >= CYCLES_MODE2) {
                /* Started pixel transfer — render the scanline */
                ppu->mode3_length = compute_mode3_length(mem, ppu->ly);
                render_scanline(ppu, mem, ppu->ly);
            }
        } else {
            ppu->mode = PPU_MODE_HBLANK;
            update_stat(ppu, mem);
        }
    } else {
        /* VBlank scanlines (LY 144-153) */
        if (ppu->cycles >= CYCLES_PER_SCANLINE) {
            ppu->cycles -= CYCLES_PER_SCANLINE;
            ppu->ly++;
            mem->io[0x44] = ppu->ly;
            if (ppu->ly >= SCANLINES_TOTAL) {
                /* Start new frame */
                ppu->ly = 0;
                mem->io[0x44] = 0;
                ppu->mode = PPU_MODE_OAM;
                ppu->mode3_length = compute_mode3_length(mem, 0);
                DBG(PPU, "Frame  LY=0 mode=OAM");
                update_stat(ppu, mem);
            } else {
                /* Still in VBlank */
                ppu->mode = PPU_MODE_VBLANK;
                update_stat(ppu, mem);
            }
        }
    }
}
