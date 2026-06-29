#include "memory.h"
#include "joypad.h"
#include "timer.h"
#include "apu.h"
#include "dbg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const u8 bootix_dmg[0x100] = {
    0x31, 0xFE, 0xFF, 0x21, 0xFF, 0x9F, 0xAF, 0x32, 0xCB, 0x7C, 0x20, 0xFA, 0x0E, 0x11, 0x21, 0x26,
    0xFF, 0x3E, 0x80, 0x32, 0xE2, 0x0C, 0x3E, 0xF3, 0x32, 0xE2, 0x0C, 0x3E, 0x77, 0x32, 0xE2, 0x11,
    0x04, 0x01, 0x21, 0x10, 0x80, 0x1A, 0xCD, 0xB8, 0x00, 0x1A, 0xCB, 0x37, 0xCD, 0xB8, 0x00, 0x13,
    0x7B, 0xFE, 0x34, 0x20, 0xF0, 0x11, 0xCC, 0x00, 0x06, 0x08, 0x1A, 0x13, 0x22, 0x23, 0x05, 0x20,
    0xF9, 0x21, 0x04, 0x99, 0x01, 0x0C, 0x01, 0xCD, 0xB1, 0x00, 0x3E, 0x19, 0x77, 0x21, 0x24, 0x99,
    0x0E, 0x0C, 0xCD, 0xB1, 0x00, 0x3E, 0x91, 0xE0, 0x40, 0x06, 0x10, 0x11, 0xD4, 0x00, 0x78, 0xE0,
    0x43, 0x05, 0x7B, 0xFE, 0xD8, 0x28, 0x04, 0x1A, 0xE0, 0x47, 0x13, 0x0E, 0x1C, 0xCD, 0xA7, 0x00,
    0xAF, 0x90, 0xE0, 0x43, 0x05, 0x0E, 0x1C, 0xCD, 0xA7, 0x00, 0xAF, 0xB0, 0x20, 0xE0, 0xE0, 0x43,
    0x3E, 0x83, 0xCD, 0x9F, 0x00, 0x0E, 0x27, 0xCD, 0xA7, 0x00, 0x3E, 0xC1, 0xCD, 0x9F, 0x00, 0x11,
    0x8A, 0x01, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x1B, 0x7A, 0xB3, 0x20, 0xF5, 0x18, 0x49, 0x0E,
    0x13, 0xE2, 0x0C, 0x3E, 0x87, 0xE2, 0xC9, 0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA, 0x0D, 0x20, 0xF7,
    0xC9, 0x78, 0x22, 0x04, 0x0D, 0x20, 0xFA, 0xC9, 0x47, 0x0E, 0x04, 0xAF, 0xC5, 0xCB, 0x10, 0x17,
    0xC1, 0xCB, 0x10, 0x17, 0x0D, 0x20, 0xF5, 0x22, 0x23, 0x22, 0x23, 0xC9, 0x3C, 0x42, 0xB9, 0xA5,
    0xB9, 0xA5, 0x42, 0x3C, 0x00, 0x54, 0xA8, 0xFC, 0x42, 0x4F, 0x4F, 0x54, 0x49, 0x58, 0x2E, 0x44,
    0x4D, 0x47, 0x20, 0x76, 0x31, 0x2E, 0x32, 0x00, 0x3E, 0xFF, 0xC6, 0x01, 0x0B, 0x1E, 0xD8, 0x21,
    0x4D, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3E, 0x01, 0xE0, 0x50
};


void mem_init(mem_t *m) {
    memset(m, 0, sizeof(*m));
    m->rom = NULL; m->eram = NULL;
    m->rom_banks = 2; m->ram_banks = 0;
    m->mbc_type = MBC_NONE; m->mbc_rom_bank = 1;
    m->io[0x00] = 0xCF; m->io[0x04] = 0x00;
    m->io[0x05] = 0x00; m->io[0x06] = 0x00;
    m->io[0x07] = 0x00; m->io[0x0F] = 0xE1;
    m->io[0x40] = 0x91; m->io[0x41] = 0x85;
    m->io[0x42] = 0x00; m->io[0x43] = 0x00;
    m->io[0x44] = 0x00; m->io[0x45] = 0x00;
    m->io[0x47] = 0xFC; m->io[0x48] = 0xFF;
    m->io[0x49] = 0xFF; m->io[0x4A] = 0x00;
    m->dma_src = 0;
    m->dma_remaining = 0;
    m->forced_mode = GB_MODE_AUTO;
    m->active_mode = GB_MODE_DMG;
    m->cgb = false;
    m->sgb = false;
    m->cart_cgb = false;
    m->cgb_vbk = 0;
    m->cgb_svbk = 1;
    memset(m->bg_palette, 0xFF, sizeof(m->bg_palette));
    memset(m->obj_palette, 0xFF, sizeof(m->obj_palette));
    m->cgb_bcps = 0;
    m->cgb_ocps = 0;
    m->cgb_hdma5 = 0xFF;
    m->cgb_hdma_src = 0;
    m->cgb_hdma_dst = 0;
    m->cgb_hdma_active = false;
    m->cgb_key1 = 0;
}

static bool is_battery_cart(u8 cart_type) {
    switch (cart_type) {
        case 0x03: case 0x06: case 0x09: case 0x0D:
        case 0x13: case 0x1B: case 0x1E: case 0x22: case 0xFF:
            return true;
        default: return false;
    }
}
static int mbc(u8 t) {
    switch(t) {
        case 0x00: return MBC_NONE;
        case 0x01: case 0x02: case 0x03: return MBC1;
        case 0x05: case 0x06: return MBC_NONE;
        case 0x08: case 0x09: return MBC_NONE;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: return MBC3;
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: return MBC5;
        default: return MBC_NONE;
    }
}

static void resolve_model(mem_t *m, u8 cgb_flag, u8 sgb_flag)
{
    m->cart_cgb = cgb_flag != 0;
    switch (m->forced_mode) {
        case GB_MODE_DMG: m->active_mode = GB_MODE_DMG; break;
        case GB_MODE_CGB: m->active_mode = GB_MODE_CGB; break;
        case GB_MODE_SGB: m->active_mode = GB_MODE_SGB; break;
        case GB_MODE_AUTO:
        default:
            if (cgb_flag) m->active_mode = GB_MODE_CGB;
            else if (sgb_flag) m->active_mode = GB_MODE_SGB;
            else m->active_mode = GB_MODE_DMG;
            break;
    }
    m->cgb = m->active_mode == GB_MODE_CGB;
    m->sgb = m->active_mode == GB_MODE_SGB;
}

bool mem_load_rom(mem_t *m, const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz < 0x150) { fclose(f); return false; }
    u8 h[0x50]; fseek(f, 0x100, SEEK_SET); fread(h, 1, 0x50, f); rewind(f);
    memset(m->rom_title, 0, 17);
    for (int i = 0; i < 16 && h[i+0x34] >= 0x20 && h[i+0x34] < 0x7F; i++)
        m->rom_title[i] = h[i+0x34];
    m->mbc_type = mbc(h[0x47]);
    resolve_model(m, (u8)((h[0x43] & 0x80) != 0), (u8)(h[0x46] == 0x03));
    m->battery_backed = is_battery_cart(h[0x47]);
    m->rom_banks = h[0x48] <= 8 ? (1 << (h[0x48] + 1)) : 512;
    m->ram_banks = h[0x49] <= 4 ? (1 << (h[0x49] + 2)) : 0;
    if (m->ram_banks < 1) m->ram_banks = 0;
    free(m->rom); free(m->eram); m->rom = NULL; m->eram = NULL;
    int rs = m->rom_banks * ROM_BANK_SIZE;
    m->rom = malloc(rs);
    if (!m->rom) { fclose(f); return false; }
    memset(m->rom, 0xFF, rs); fread(m->rom, 1, sz < rs ? sz : rs, f); fclose(f);
    if (m->ram_banks > 0)
        m->eram = calloc(1, m->ram_banks * 0x2000);
    m->mbc_rom_bank = 1; m->mbc_ram_bank = 0; m->mbc_mode = 0; m->mbc_ram_enable = false;
    /* Build .sav path and load SRAM if battery-backed */
    snprintf(m->sav_path, sizeof(m->sav_path), "%s", p);
    size_t len = strlen(m->sav_path);
    if (len >= 4) strcpy(m->sav_path + len - 4, ".sav");
    if (m->battery_backed && m->eram && m->ram_banks > 0) {
        FILE *sf = fopen(m->sav_path, "rb");
        if (sf) {
            fread(m->eram, 1, m->ram_banks * 0x2000, sf);
            fclose(sf);
            fprintf(stderr, "Loaded SRAM: %s\n", m->sav_path);
        }
    }
    return true;
}

static void mbc_write(mem_t *m, u16 a, u8 v) {
    int bank;
    switch (m->mbc_type) {
        case MBC1:
            if (a < 0x2000) m->mbc_ram_enable = ((v & 0x0F) == 0x0A);
            else if (a < 0x4000) { m->mbc_rom_bank = (m->mbc_rom_bank & 0x60) | (v & 0x1F); if ((m->mbc_rom_bank & 0x1F) == 0) m->mbc_rom_bank |= 1; }
            else if (a < 0x6000) { m->mbc_ram_bank = m->mbc_mode ? (v & 3) : 0; m->mbc_rom_bank = ((v & 3) << 5) | (m->mbc_rom_bank & 0x1F); if ((m->mbc_rom_bank & 0x1F) == 0) m->mbc_rom_bank |= 1; }
            else m->mbc_mode = v & 1;
            m->mbc_rom_bank %= m->rom_banks; if (m->mbc_rom_bank == 0) m->mbc_rom_bank = 1;
            break;
        case MBC3:
            if (a < 0x2000) m->mbc_ram_enable = ((v & 0x0F) == 0x0A);
            else if (a < 0x4000) { bank = v & 0x7F; if (bank == 0) bank = 1; m->mbc_rom_bank = bank; }
            else if (a < 0x6000) m->mbc_ram_bank = v & 3;
            break;
        case MBC5:
            if (a < 0x2000) m->mbc_ram_enable = ((v & 0x0F) == 0x0A);
            else if (a < 0x3000) m->mbc_rom_bank = (m->mbc_rom_bank & 0x100) | v;
            else if (a < 0x4000) m->mbc_rom_bank = ((v & 1) << 8) | (m->mbc_rom_bank & 0xFF);
            else if (a < 0x6000) m->mbc_ram_bank = v & 0x0F;
            m->mbc_rom_bank %= m->rom_banks; if (m->mbc_rom_bank == 0) m->mbc_rom_bank = 1;
            break;
        default: break;
    }
}

u8 mem_read(mem_t *m, u16 a) {
    if (m->boot_on && a < 0x0100) { DBG(MEM, "R %04X (bootrom) = %02X", a, bootix_dmg[a]); return bootix_dmg[a]; }
    if (a < 0x4000) return m->rom[a];
    if (a < 0x8000) return m->rom[m->mbc_rom_bank * ROM_BANK_SIZE + (a - 0x4000)];
    if (a < 0xA000) return m->vram_banks[m->cgb ? (m->cgb_vbk & 1) : 0][a & 0x1FFF];
    if (a < 0xC000) { if (m->mbc_ram_enable && m->eram && m->ram_banks > 0) return m->eram[m->mbc_ram_bank * 0x2000 + (a & 0x1FFF)]; return 0xFF; }
    if (a < 0xD000) return m->wram_banks[0][a & 0x0FFF];
    if (a < 0xE000) return m->wram_banks[m->cgb ? (m->cgb_svbk & 7) : 1][a & 0x0FFF];
    if (a < 0xFE00) { u16 m2 = a - 0x2000; return m->wram_banks[m2 < 0xD000 ? 0 : (m->cgb ? (m->cgb_svbk & 7) : 1)][m2 & 0x0FFF]; }
    if (a < 0xFEA0) {
        if (m->dma_remaining > 0) { DBG(MEM, "R %04X (OAM blocked by DMA)", a); return 0xFF; }
        return m->oam[a & 0xFF];
    }
    if (a < 0xFF00) return 0xFF;
    if (a < 0xFF80) {
        if (a == 0xFF00 && m->joypad) return joypad_read((joypad_t*)m->joypad);
        if (a >= 0xFF10 && a <= 0xFF3F && m->apu) return apu_read((apu_t*)m->apu, a);
        if (m->cgb) {
            u8 r = a & 0x7F;
            if (r == 0x4D) return m->cgb_key1 | 0x7E; /* KEY1 */
            if (r == 0x4F) return m->cgb_vbk | 0xFE; /* VBK */
            if (r == 0x55) return m->cgb_hdma5;       /* HDMA5 */
            if (r == 0x68) return m->cgb_bcps | 0x40; /* BCPS */
            if (r == 0x69) return m->bg_palette[m->cgb_bcps & 0x3F]; /* BCPD */
            if (r == 0x6A) return m->cgb_ocps | 0x40; /* OCPS */
            if (r == 0x6B) return m->obj_palette[m->cgb_ocps & 0x3F]; /* OCPD */
            if (r == 0x70) return m->cgb_svbk | 0xF8; /* SVBK */
        }
        u8 v = m->io[a & 0x7F];
        DBG(MEM, "R %04X = %02X", a, v);
        return v;
    }
    if (a < 0xFFFF) return m->hram[a & 0x7F];
    DBG(MEM, "R %04X (IE) = %02X", a, m->ie);
    return m->ie;
}

void mem_write(mem_t *m, u16 a, u8 v) {
    if (a < 0x8000) { mbc_write(m, a, v); return; }
    if (a < 0xA000) { m->vram_banks[m->cgb ? (m->cgb_vbk & 1) : 0][a & 0x1FFF] = v; return; }
    if (a < 0xC000) { if (m->mbc_ram_enable && m->eram && m->ram_banks > 0) m->eram[m->mbc_ram_bank * 0x2000 + (a & 0x1FFF)] = v; return; }
    if (a >= 0xFE00 && a < 0xFEA0 && m->dma_remaining > 0) { DBG(MEM, "W %04X (OAM blocked by DMA)", a); return; }
    if (a < 0xD000) { m->wram_banks[0][a & 0x0FFF] = v; return; }
    if (a < 0xE000) { m->wram_banks[(m->cgb ? ((m->cgb_svbk & 7) ? (m->cgb_svbk & 7) : 1) : 1)][a & 0x0FFF] = v; return; }
    if (a < 0xFE00) { u16 m2 = a - 0x2000; m->wram_banks[m2 < 0xD000 ? 0 : (m->cgb ? ((m->cgb_svbk & 7) ? (m->cgb_svbk & 7) : 1) : 1)][m2 & 0x0FFF] = v; return; }
    if (a < 0xFEA0) { m->oam[a & 0xFF] = v; return; }
    if (a < 0xFF00) return;
    if (a < 0xFF80) {
        u8 r = a & 0x7F;
        if (r == 0x00) {
            u8 sel = v & 0x30;
            m->io[0x00] = sel;
            DBG(MEM, "W FF00 = %02X (sel=%s%s)", v, (sel & 0x20) ? "" : "buttons ", (sel & 0x10) ? "" : "dpad ");
            if (m->joypad) {
                joypad_t *jp = (joypad_t *)m->joypad;
                jp->select_buttons = !(sel & 0x20);
                jp->select_dpad = !(sel & 0x10);
            }
            return;
        }
        if (r == 0x01) { m->io[0x01] = v; DBG(MEM, "W FF01 (SB) = %02X", v); return; }
        if (r == 0x02) { m->io[0x02] = v; DBG(MEM, "W FF02 (SC) = %02X", v); return; }
        if (r == 0x04) { if (m->timer) { timer_write_div((timer_t *)m->timer, m); } else { m->io[0x04] = 0; } return; }
        if (r == 0x05) { m->io[0x05] = v; DBG(TIMER, "TIMA write = %02X", v); return; }
        if (r == 0x06) { m->io[0x06] = v; DBG(TIMER, "TMA write = %02X", v); return; }
        if (r == 0x07) { if (m->timer) { timer_write_tac((timer_t *)m->timer, m, v); } else { m->io[0x07] = v & 0x07; } return; }
        if (r == 0x0F) { m->io[0x0F] = v & 0x1F; DBG(INT, "IF write %02X (old %02X)", v, m->io[r]); return; }
        if (r == 0x40) { DBG(PPU, "LCDC write %02X", v); m->io[0x40] = v; return; }
        if (r == 0x41) { DBG(PPU, "STAT write %02X (old %02X)", v, m->io[0x41]); m->io[0x41] = v; return; }
        if (r == 0x42 || r == 0x43 || r == 0x45 || r == 0x4A || r == 0x4B) { m->io[r] = v; return; }
        if (r == 0x46) {
            m->io[0x46] = v;
            m->dma_src = v;
            m->dma_remaining = 0xA0;
            DBG(DMA, "START src=%02X00 dst=FE00 len=A0", v);
            return;
        }
        if (r == 0x47 || r == 0x48 || r == 0x49) { m->io[r] = v; return; }
        if (r == 0x50) { DBG(PPU, "Boot ROM disable write %02X", v); m->boot_on = false; m->io[0x50] = v; return; }
        if (m->cgb) {
            if (r == 0x4D) { m->cgb_key1 = (m->cgb_key1 & 0x80) | (v & 0x01); return; }
            if (r == 0x4F) { m->cgb_vbk = v & 1; return; }
            if (r == 0x51) { m->cgb_hdma_src = (m->cgb_hdma_src & 0xFF00) | ((u16)v << 8); return; }
            if (r == 0x52) { m->cgb_hdma_src = (m->cgb_hdma_src & 0xFF00) | (v & 0xF0); return; }
            if (r == 0x53) { m->cgb_hdma_dst = (m->cgb_hdma_dst & 0xFF00) | ((u16)(v & 0x1F) << 8); return; }
            if (r == 0x54) { m->cgb_hdma_dst = (m->cgb_hdma_dst & 0xFF00) | (v & 0xF0); return; }
            if (r == 0x55) {
                m->cgb_hdma5 = v;
                m->cgb_hdma_active = true;
                return;
            }
            if (r == 0x68) { m->cgb_bcps = v; return; }
            if (r == 0x69) {
                m->bg_palette[m->cgb_bcps & 0x3F] = v;
                if (m->cgb_bcps & 0x80) m->cgb_bcps = (m->cgb_bcps & 0xC0) | ((m->cgb_bcps + 1) & 0x3F);
                return;
            }
            if (r == 0x6A) { m->cgb_ocps = v; return; }
            if (r == 0x6B) {
                m->obj_palette[m->cgb_ocps & 0x3F] = v;
                if (m->cgb_ocps & 0x80) m->cgb_ocps = (m->cgb_ocps & 0xC0) | ((m->cgb_ocps + 1) & 0x3F);
                return;
            }
            if (r == 0x70) { m->cgb_svbk = v & 7; return; }
        }
        /* Other IO registers (audio, etc.) */
        if (r >= 0x10 && r <= 0x3F && m->apu) { apu_write((apu_t*)m->apu, a, v); m->io[r] = v; return; }
        if (r >= 0x4C) { m->io[r] = v; return; }
        return;
    }
    if (a < 0xFFFF) { m->hram[a & 0x7F] = v; return; }
    DBG(INT, "IE write %02X (old %02X)", v, m->ie);
    m->ie = v;
}

void mem_write16(mem_t *m, u16 a, u16 v) { mem_write(m, a, v & 0xFF); mem_write(m, a+1, v >> 8); }

void mem_dma_tick(mem_t *m, int cycles)
{
    if (m->dma_remaining <= 0) return;
    int idx = 0xA0 - m->dma_remaining;
    for (int i = 0; i < cycles && m->dma_remaining > 0; i++) {
        if ((i & 3) == 0) {
            u16 src = ((u16)m->dma_src << 8) + idx;
            m->oam[idx] = mem_read(m, src);
            idx++;
            m->dma_remaining--;
        }
    }
    DBG(DMA, "DONE %d bytes transferred", idx);
}

void mem_save_sram(mem_t *m) {
    if (!m->battery_backed || !m->eram || m->ram_banks <= 0) return;
    FILE *f = fopen(m->sav_path, "wb");
    if (!f) { fprintf(stderr, "Failed to save SRAM: %s\n", m->sav_path); return; }
    fwrite(m->eram, 1, m->ram_banks * 0x2000, f);
    fclose(f);
    fprintf(stderr, "Saved SRAM: %s\n", m->sav_path);
}
