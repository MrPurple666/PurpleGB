#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "memory.h"

#define INT_VBLANK 0
#define INT_STAT   1
#define INT_TIMER  2
#define INT_SERIAL 3
#define INT_JOYPAD 4

static inline void interrupt_request(mem_t *mem, int bit) { mem->io[0x0F] |= (1 << bit); }
static inline bool interrupt_pending(mem_t *mem) { return (mem->io[0x0F] & mem->ie & 0x1F) != 0; }
static inline u8 interrupt_get_pending(mem_t *mem) { return mem->io[0x0F] & mem->ie & 0x1F; }

#endif
