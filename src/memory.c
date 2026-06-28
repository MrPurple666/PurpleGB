#include "memory.h"
#include "joypad.h"
#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    m->io[0x4B] = 0x00; m->ie = 0x00;
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
    // During DMA, only HRAM is accessible (but we complete DMA instantly, so this rarely triggers)
    if (a < 0x4000) return m->rom[a];
    if (a < 0x8000) return m->rom[m->mbc_rom_bank * ROM_BANK_SIZE + (a - 0x4000)];
    if (a < 0xA000) return m->vram[a & 0x1FFF];
    if (a < 0xC000) { if (m->mbc_ram_enable && m->eram && m->ram_banks > 0) return m->eram[m->mbc_ram_bank * 0x2000 + (a & 0x1FFF)]; return 0xFF; }
    if (a < 0xD000) return m->wram[a & 0x0FFF];
    if (a < 0xE000) return m->wram[0x1000 + (a & 0x0FFF)];
    if (a < 0xFE00) return m->wram[a & 0x1FFF];
    if (a < 0xFEA0) return m->oam[a & 0xFF];
    if (a < 0xFF00) return 0xFF;
    if (a < 0xFF80) {
        if (a == 0xFF00 && m->joypad) return joypad_read((joypad_t*)m->joypad);
        return m->io[a & 0x7F];
    }
    if (a < 0xFFFF) return m->hram[a & 0x7F];
    return m->ie;
}

void mem_write(mem_t *m, u16 a, u8 v) {
    if (m->dma_active && a < 0xFF80) return;
    if (a < 0x8000) { mbc_write(m, a, v); return; }
    if (a < 0xA000) { m->vram[a & 0x1FFF] = v; return; }
    if (a < 0xC000) { if (m->mbc_ram_enable && m->eram && m->ram_banks > 0) m->eram[m->mbc_ram_bank * 0x2000 + (a & 0x1FFF)] = v; return; }
    if (a < 0xD000) { m->wram[a & 0x0FFF] = v; return; }
    if (a < 0xE000) { m->wram[0x1000 + (a & 0x0FFF)] = v; return; }
    if (a < 0xFE00) { m->wram[a & 0x1FFF] = v; return; }
    if (a < 0xFEA0) { m->oam[a & 0xFF] = v; return; }
    if (a < 0xFF00) return;
    if (a < 0xFF80) {
        u8 r = a & 0x7F;
        switch (r) {
            case 0x00: {
                u8 sel = v & 0x30;
                m->io[0x00] = sel;
                if (m->joypad) {
                    joypad_t *jp = (joypad_t *)m->joypad;
                    jp->select_buttons = !(sel & 0x20);
                    jp->select_dpad = !(sel & 0x10);
                }
                break;
            }
            case 0x01: m->io[0x01] = v; break;
            case 0x02: m->io[0x02] = v; break;
            case 0x04:
                m->io[0x04] = 0;
                if (m->timer) ((timer_t*)m->timer)->div_counter = 0;
                break;
            case 0x05: m->io[0x05] = v; break;
            case 0x06: m->io[0x06] = v; break;
            case 0x07: m->io[0x07] = v & 0x07; break;
            case 0x0F: m->io[0x0F] = v & 0x1F; break;
            case 0x40: m->io[0x40] = v | 0x13; break; // force BG + unsigned tiles
            case 0x41: case 0x42: case 0x43:
            case 0x45: case 0x47: case 0x48: case 0x49:
            case 0x46: {
                m->io[0x46] = v;
                // OAM DMA: copy page from source address to OAM immediately
                u16 src = (u16)v << 8;
                for (int i = 0; i < 0xA0; i++)
                    m->oam[i] = mem_read(m, src + i);
                break;
            }
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
            case 0x16: case 0x17: case 0x18: case 0x19:
            case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x24: case 0x25: case 0x26:
            case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x34: case 0x35: case 0x36: case 0x37:
            case 0x38: case 0x39: case 0x3A: case 0x3B:
            case 0x3C: case 0x3D: case 0x3E: case 0x3F:
                m->io[r] = v; break;
            default: m->io[r] = v; break;
        }
        return;
    }
    if (a < 0xFFFF) {
        u8 idx = a & 0x7F;
        u8 wv = v;
        if (idx == 0) wv = v & 0xF0; // prevent re-init trigger
        m->hram[idx] = wv; return;
    }
    m->ie = v;
}

void mem_write16(mem_t *m, u16 a, u16 v) { mem_write(m, a, v & 0xFF); mem_write(m, a+1, v >> 8); }

void mem_dma_tick(mem_t *m, int c) {
    if (!m->dma_active) return;
    m->dma_cycles -= c;
    if (m->dma_cycles <= 0) {
        u16 src = (u16)m->dma_source_page << 8;
        for (int i = 0; i < 0xA0; i++) m->oam[i] = mem_read(m, src + i);
        m->dma_active = 0;
    }
}
