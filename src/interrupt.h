#ifndef INTERRUPT_H
#define INTERRUPT_H

#include "memory.h"
#include "dbg.h"

#define INT_VBLANK 0
#define INT_STAT   1
#define INT_TIMER  2
#define INT_SERIAL 3
#define INT_JOYPAD 4

static inline void interrupt_request(mem_t *mem, int bit) { mem->io[0x0F] |= (1 << bit); DBG(INT, "Request INT bit %d  IF=%02X IE=%02X", bit, mem->io[0x0F], mem->ie); }
static inline bool interrupt_pending(mem_t *mem) { u8 p = mem->io[0x0F] & mem->ie & 0x1F; if (p) DBG(INT, "Pending Ints=%02X IF=%02X IE=%02X", p, mem->io[0x0F], mem->ie); return p; }
static inline u8 interrupt_get_pending(mem_t *mem) { return mem->io[0x0F] & mem->ie & 0x1F; }

#endif
