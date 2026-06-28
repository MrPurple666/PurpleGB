#include "timer.h"
#include "interrupt.h"

void timer_init(timer_t *timer)
{
    timer->div_counter = 0;
}

static const int timer_bits[4] = { 9, 3, 5, 7 };

void timer_tick(timer_t *timer, mem_t *mem, int cycles)
{
    for (int i = 0; i < cycles; i++) {
        u16 prev_div = timer->div_counter;
        timer->div_counter++;

        u8 tac = mem->io[0x07];
        if (tac & 0x04) {
            int bit = timer_bits[tac & 0x03];
            if (((prev_div >> bit) & 1) && !((timer->div_counter >> bit) & 1)) {
                u8 tima = mem->io[0x05] + 1;
                if (tima == 0) {
                    tima = mem->io[0x06];
                    interrupt_request(mem, INT_TIMER);
                }
                mem->io[0x05] = tima;
            }
        }

        mem->io[0x04] = timer->div_counter >> 8;
    }
}
