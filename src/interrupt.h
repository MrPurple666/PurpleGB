#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "memory.h"

#define INT_VBLANK 0
#define INT_STAT   1
#define INT_TIMER  2
#define INT_SERIAL 3
#define INT_JOYPAD 4

void interrupt_request(mem_t *mem, int bit);
bool interrupt_pending(mem_t *mem);
u8   interrupt_get_pending(mem_t *mem);

#endif
