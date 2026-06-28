#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "joypad.h"

void mem_init(mem_t *mem)
{
    memset(mem, 0, sizeof(*mem));
    mem->rom = NULL;
    mem->eram = NULL;
    mem->rom_banks = 2;
    mem->ram_banks = 0;
    mem->mbc_type = MBC_NONE;
    mem->mbc_rom_bank = 1;
    mem->mbc_ram_bank = 0;
    mem->mbc_mode = 0;
    mem->mbc_ram_enable = false;
    mem->dma_active = false;
    mem->dma_cycles = 0;
    mem->dma_source_page = 0;
    mem->io[0x00] = 0xCF;
    mem->io[0x04] = 0x00;
    mem->io[0x05] = 0x00;
    mem->io[0x06] = 0x00;
    mem->io[0x07] = 0x00;
    mem->io[0x0F] = 0xE1;
    mem->io[0x40] = 0x91;
    mem->io[0x41] = 0x85;
    mem->io[0x42] = 0x00;
    mem->io[0x43] = 0x00;
    mem->io[0x44] = 0x00;
    mem->io[0x45] = 0x00;
    mem->io[0x47] = 0xFC;
    mem->io[0x48] = 0xFF;
    mem->io[0x49] = 0xFF;
    mem->io[0x4A] = 0x00;
    mem->io[0x4B] = 0x00;
    mem->ie = 0x00;
}

static int get_mbc_type(u8 type_byte)
{
    switch (type_byte) {
        case 0x00: return MBC_NONE;
        case 0x01: case 0x02: case 0x03: return MBC1;
        case 0x05: case 0x06: return MBC_NONE;
        case 0x08: case 0x09: return MBC_NONE;
        case 0x0F: case 0x10: case 0x11:
        case 0x12: case 0x13: return MBC3;
        case 0x19: case 0x1A: case 0x1B:
        case 0x1C: case 0x1D: case 0x1E: return MBC5;
        default: return MBC_NONE;
    }
}

static int rom_size_to_banks(u8 size_byte)
{
    switch (size_byte) {
        case 0: return 2;   case 1: return 4;   case 2: return 8;
        case 3: return 16;  case 4: return 32;  case 5: return 64;
        case 6: return 128; case 7: return 256; case 8: return 512;
        default: return 2;
    }
}

static int ram_size_to_banks(u8 size_byte)
{
    switch (size_byte) {
        case 0: return 0; case 1: return 1; case 2: return 1;
        case 3: return 4; case 4: return 16;
        default: return 0;
    }
}

bool mem_load_rom(mem_t *mem, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Failed to open ROM: %s\n", path); return false; }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size < 0x150) { fprintf(stderr, "ROM too small\n"); fclose(f); return false; }

    u8 header[0x50];
    fseek(f, 0x100, SEEK_SET);
    fread(header, 1, 0x50, f);
    rewind(f);

    memset(mem->rom_title, 0, sizeof(mem->rom_title));
    for (int i = 0; i < 16 && header[i + 0x34] >= 0x20 && header[i + 0x34] < 0x7F; i++)
        mem->rom_title[i] = header[i + 0x34];

    mem->mbc_type = get_mbc_type(header[0x47]);
    mem->rom_banks = rom_size_to_banks(header[0x48]);
    mem->ram_banks = ram_size_to_banks(header[0x49]);

    free(mem->rom); free(mem->eram);
    mem->rom = NULL; mem->eram = NULL;

    int rom_size = mem->rom_banks * ROM_BANK_SIZE;
    mem->rom = malloc(rom_size);
    if (!mem->rom) { fclose(f); return false; }
    memset(mem->rom, 0xFF, rom_size);
    fread(mem->rom, 1, size < rom_size ? size : rom_size, f);
    fclose(f);

    if (mem->ram_banks > 0) {
        mem->eram = calloc(1, mem->ram_banks * 0x2000);
        if (!mem->eram) return false;
    }

    mem->mbc_rom_bank = 1;
    mem->mbc_ram_bank = 0;
    mem->mbc_mode = 0;
    mem->mbc_ram_enable = false;

    printf("Loaded: %s (%d banks, MBC%d, %d RAM banks)\n",
           mem->rom_title, mem->rom_banks,
           mem->mbc_type == MBC1 ? 1 : (mem->mbc_type == MBC3 ? 3 :
           mem->mbc_type == MBC5 ? 5 : 0), mem->ram_banks);
    return true;
}

static void mbc_write(mem_t *mem, u16 addr, u8 val)
{
    switch (mem->mbc_type) {
        case MBC1:
            if (addr < 0x2000) mem->mbc_ram_enable = ((val & 0x0F) == 0x0A);
            else if (addr < 0x4000) { mem->mbc_rom_bank = (mem->mbc_rom_bank & 0x60) | (val & 0x1F); if ((mem->mbc_rom_bank & 0x1F) == 0) mem->mbc_rom_bank |= 1; }
            else if (addr < 0x6000) { mem->mbc_ram_bank = mem->mbc_mode ? (val & 0x03) : 0; mem->mbc_rom_bank = ((val & 0x03) << 5) | (mem->mbc_rom_bank & 0x1F); if ((mem->mbc_rom_bank & 0x1F) == 0) mem->mbc_rom_bank |= 1; }
            else mem->mbc_mode = val & 1;
            mem->mbc_rom_bank %= mem->rom_banks; if (mem->mbc_rom_bank == 0) mem->mbc_rom_bank = 1;
            break;
        case MBC3:
            if (addr < 0x2000) mem->mbc_ram_enable = ((val & 0x0F) == 0x0A);
            else if (addr < 0x4000) { mem->mbc_rom_bank = val & 0x7F; if (mem->mbc_rom_bank == 0) mem->mbc_rom_bank = 1; }
            else if (addr < 0x6000) mem->mbc_ram_bank = val & 0x03;
            break;
        case MBC5:
            if (addr < 0x2000) mem->mbc_ram_enable = ((val & 0x0F) == 0x0A);
            else if (addr < 0x3000) mem->mbc_rom_bank = (mem->mbc_rom_bank & 0x100) | val;
            else if (addr < 0x4000) mem->mbc_rom_bank = ((val & 1) << 8) | (mem->mbc_rom_bank & 0xFF);
            else if (addr < 0x6000) mem->mbc_ram_bank = val & 0x0F;
            mem->mbc_rom_bank %= mem->rom_banks; if (mem->mbc_rom_bank == 0) mem->mbc_rom_bank = 1;
            break;
        default: break;
    }
}

u8 mem_read(mem_t *mem, u16 addr)
{
    if (mem->dma_active) {
        if (addr >= 0xFF80 && addr <= 0xFFFE) return mem->hram[addr & 0x7F];
        return 0xFF;
    }
    if (addr < 0x4000) return mem->rom[addr];
    if (addr < 0x8000) return mem->rom[mem->mbc_rom_bank * ROM_BANK_SIZE + (addr - 0x4000)];
    if (addr < 0xA000) return mem->vram[addr & 0x1FFF];
    if (addr < 0xC000) {
        if (mem->mbc_ram_enable && mem->eram && mem->ram_banks > 0)
            return mem->eram[mem->mbc_ram_bank * 0x2000 + (addr & 0x1FFF)];
        return 0xFF;
    }
    if (addr < 0xD000) return mem->wram[addr & 0x0FFF];
    if (addr < 0xE000) return mem->wram[0x1000 + (addr & 0x0FFF)];
    if (addr < 0xFE00) return mem->wram[addr & 0x1FFF];
    if (addr < 0xFEA0) return mem->oam[addr & 0xFF];
    if (addr < 0xFF00) return 0xFF;
        if (addr < 0xFF80) {
        if (addr == 0xFF00 && mem->joypad)
            return joypad_read((joypad_t *)mem->joypad);
        return mem->io[addr & 0x7F];
    }
    if (addr < 0xFFFF) return mem->hram[addr & 0x7F];
    return mem->ie;
}

void mem_write(mem_t *mem, u16 addr, u8 val)
{
    if (mem->dma_active && addr < 0xFF80) return;
    if (addr < 0x8000) { mbc_write(mem, addr, val); return; }
    if (addr < 0xA000) { mem->vram[addr & 0x1FFF] = val; return; }
    if (addr < 0xC000) {
        if (mem->mbc_ram_enable && mem->eram && mem->ram_banks > 0)
            mem->eram[mem->mbc_ram_bank * 0x2000 + (addr & 0x1FFF)] = val;
        return;
    }
    if (addr < 0xD000) { mem->wram[addr & 0x0FFF] = val; return; }
    if (addr < 0xE000) { mem->wram[0x1000 + (addr & 0x0FFF)] = val; return; }
    if (addr < 0xFE00) { mem->wram[addr & 0x1FFF] = val; return; }
    if (addr < 0xFEA0) { mem->oam[addr & 0xFF] = val; return; }
    if (addr < 0xFF00) return;
    if (addr < 0xFF80) {
        u8 reg = addr & 0x7F;
        switch (reg) {
            case 0x00: {
                u8 sel = val & 0x30;
                mem->io[0x00] = sel;
                if (mem->joypad) {
                    joypad_t *jp = (joypad_t *)mem->joypad;
                    jp->select_buttons = !(sel & 0x20);
                    jp->select_dpad    = !(sel & 0x10);
                }
                break;
            }
            case 0x01: mem->io[0x01] = val; break;
            case 0x02: mem->io[0x02] = val; break;
            case 0x04: mem->io[0x04] = 0; break;
            case 0x05: mem->io[0x05] = val; break;
            case 0x06: mem->io[0x06] = val; break;
            case 0x07: mem->io[0x07] = val & 0x07; break;
            case 0x0F: mem->io[0x0F] = val & 0x1F; break;
            case 0x40: case 0x41: case 0x42: case 0x43:
            case 0x45: case 0x47: case 0x48: case 0x49:
            case 0x4A: case 0x4B: mem->io[reg] = val; break;
            case 0x46:
                mem->io[0x46] = val;
                mem->dma_active = true;
                mem->dma_cycles = 160;
                mem->dma_source_page = val;
                break;
            case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
            case 0x16: case 0x17: case 0x18: case 0x19:
            case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
            case 0x20: case 0x21: case 0x22: case 0x23:
            case 0x24: case 0x25: case 0x26:
            case 0x30: case 0x31: case 0x32: case 0x33:
            case 0x34: case 0x35: case 0x36: case 0x37:
            case 0x38: case 0x39: case 0x3A: case 0x3B:
            case 0x3C: case 0x3D: case 0x3E: case 0x3F:
                mem->io[reg] = val; break;
            default: mem->io[reg] = val; break;
        }
        return;
    }
    if (addr < 0xFFFF) { mem->hram[addr & 0x7F] = val; return; }
    mem->ie = val;
}

void mem_write16(mem_t *mem, u16 addr, u16 val)
{
    mem_write(mem, addr, val & 0xFF);
    mem_write(mem, addr + 1, val >> 8);
}

void mem_dma_tick(mem_t *mem, int cycles)
{
    if (!mem->dma_active) return;
    mem->dma_cycles -= cycles;
    if (mem->dma_cycles <= 0) {
        u16 src = (u16)mem->dma_source_page << 8;
        for (int i = 0; i < 0xA0; i++) mem->oam[i] = mem_read(mem, src + i);
        mem->dma_active = false;
        mem->dma_cycles = 0;
    }
}
