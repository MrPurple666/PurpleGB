#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint64_t u64;

#define ROM_BANK_SIZE 0x4000
#define VRAM_SIZE     0x4000
#define WRAM_SIZE     0x8000
#define OAM_SIZE      0xA0
#define HRAM_SIZE     0x80
#define IO_SIZE       0x80

typedef enum { MBC_NONE = 0, MBC1, MBC3, MBC5 } mbc_type_t;
typedef enum { GB_MODE_AUTO = 0, GB_MODE_DMG, GB_MODE_CGB, GB_MODE_SGB } gb_mode_t;


typedef struct {
    u8 *rom;
    int rom_banks;
    u8 vram_banks[2][0x2000];
    u8 *eram;
    int ram_banks;
    u8 wram_banks[8][0x1000];
    u8 oam[OAM_SIZE], hram[HRAM_SIZE], io[IO_SIZE];
    u8 ie;
    int mbc_type;
    char rom_title[17];
    bool mbc_ram_enable;
    bool boot_on;
    bool battery_backed;
    char sav_path[512];
    int mbc_rom_bank, mbc_ram_bank, mbc_mode;
    u8 dma_src;
    int dma_remaining;
    void *joypad, *timer, *apu;
    gb_mode_t forced_mode;
    gb_mode_t active_mode;
    /* CGB/SGB fields */
    bool cgb;
    bool sgb;
    u8 bg_palette[64];
    u8 obj_palette[64];
    u8 cgb_vbk;
    u8 cgb_svbk;
    u8 cgb_bcps;
    u8 cgb_ocps;
    u8 cgb_hdma5;
    u16 cgb_hdma_src;
    u16 cgb_hdma_dst;
    bool cgb_hdma_active;
    u8 cgb_key1;
} mem_t;

void mem_init(mem_t *m);
bool mem_load_rom(mem_t *m, const char *p);
u8   mem_read(mem_t *m, u16 a);
void mem_write(mem_t *m, u16 a, u8 v);
void mem_write16(mem_t *m, u16 a, u16 v);
void mem_dma_tick(mem_t *m, int cycles);
void mem_save_sram(mem_t *m);

#endif
