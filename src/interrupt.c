#include "interrupt.h"

void interrupt_request(mem_t *mem, int bit)
{
    mem->io[0x0F] |= (1 << bit);
}

bool interrupt_pending(mem_t *mem)
{
    return (mem->io[0x0F] & mem->ie & 0x1F) != 0;
}

u8 interrupt_get_pending(mem_t *mem)
{
    return mem->io[0x0F] & mem->ie & 0x1F;
}
