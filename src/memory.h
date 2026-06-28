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
#define VRAM_SIZE     0x2000
#define WRAM_SIZE     0x2000
#define OAM_SIZE      0xA0
#define HRAM_SIZE     0x80
#define IO_SIZE       0x80

typedef enum { MBC_NONE = 0, MBC1, MBC3, MBC5 } mbc_type_t;

typedef struct {
    u8 *rom;
    int rom_size, rom_banks;
    u8 vram[VRAM_SIZE];
    u8 *eram;
    int ram_size, ram_banks;
    u8 wram[WRAM_SIZE], oam[OAM_SIZE], hram[HRAM_SIZE], io[IO_SIZE];
    u8 ie;
    int mbc_type;
    char rom_title[17];
    bool mbc_ram_enable;
    int mbc_rom_bank, mbc_ram_bank, mbc_mode;
    bool dma_active;
    int dma_cycles;
    u8 dma_source_page;
    void *joypad, *timer;
} mem_t;

void mem_init(mem_t *m);
bool mem_load_rom(mem_t *m, const char *p);
u8   mem_read(mem_t *m, u16 a);
void mem_write(mem_t *m, u16 a, u8 v);
void mem_write16(mem_t *m, u16 a, u16 v);
void mem_dma_tick(mem_t *m, int c);

void decode_logo(mem_t *m);

#endif
