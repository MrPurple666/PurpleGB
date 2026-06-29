#include "../src/ppu.h"
#include "../src/memory.h"
#include <assert.h>

static void setup(mem_t *mem, ppu_t *ppu)
{
    mem_init(mem);
    ppu_init(ppu);
    mem->io[0x40] = 0x91;
    mem->io[0x41] = 0x00;
    mem->io[0x44] = 0x00;
    mem->io[0x45] = 0x01;
}

static void test_lyc_flag_sets_when_ly_matches(void)
{
    mem_t mem;
    ppu_t ppu;
    setup(&mem, &ppu);

    ppu_tick(&ppu, &mem, 456);

    assert(mem.io[0x44] == 1);
    assert((mem.io[0x41] & 0x04) != 0);
}

static void test_lyc_flag_clears_when_ly_differs(void)
{
    mem_t mem;
    ppu_t ppu;
    setup(&mem, &ppu);

    mem.io[0x45] = 2;
    ppu_tick(&ppu, &mem, 456);

    assert(mem.io[0x44] == 1);
    assert((mem.io[0x41] & 0x04) == 0);
}

int main(void)
{
    test_lyc_flag_sets_when_ly_matches();
    test_lyc_flag_clears_when_ly_differs();
    return 0;
}
