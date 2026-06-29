#include "../src/memory.h"
#include <assert.h>
#include <string.h>

static void test_dma_transfers_oam(void)
{
    mem_t m;
    mem_init(&m);
    m.joypad = NULL;
    m.timer = NULL;

    for (int i = 0; i < 0xA0; i++)
        mem_write(&m, 0xC000 + i, i);

    mem_write(&m, 0xFF46, 0xC0);
    mem_dma_tick(&m, 640);
    assert(m.dma_remaining == 0);

    for (int i = 0; i < 0xA0; i++)
        assert(mem_read(&m, 0xFE00 + i) == (i & 0xFF));
}

static void test_dma_blocks_oam_reads(void)
{
    mem_t m;
    mem_init(&m);
    m.joypad = NULL;
    m.timer = NULL;

    mem_write(&m, 0xFF46, 0xC0);
    assert(mem_read(&m, 0xFE00) == 0xFF);
    assert(mem_read(&m, 0xFE10) == 0xFF);
    assert(mem_read(&m, 0xFE9F) == 0xFF);
}

static void test_dma_completes_after_640_cycles(void)
{
    mem_t m;
    mem_init(&m);
    m.joypad = NULL;
    m.timer = NULL;

    for (int i = 0; i < 0xA0; i++)
        mem_write(&m, 0xC000 + i, i);

    mem_write(&m, 0xFF46, 0xC0);
    mem_dma_tick(&m, 640);
    assert(m.dma_remaining == 0);

    for (int i = 0; i < 0xA0; i++)
        assert(mem_read(&m, 0xFE00 + i) == (i & 0xFF));
}

int main(void)
{
    test_dma_transfers_oam();
    test_dma_blocks_oam_reads();
    test_dma_completes_after_640_cycles();
    return 0;
}
