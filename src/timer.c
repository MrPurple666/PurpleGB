#include "timer.h"
#include "interrupt.h"

void timer_init(timer_t *timer)
{
    timer->div_counter = 0;
    timer->tima = 0;
    timer->tma = 0;
    timer->tac = 0;
}

// Bit shift amounts for each clock select value (DMG/SGB)
// 00:   4096 Hz  -> bit 9  (counter bit 9)
// 01: 262144 Hz  -> bit 3  (counter bit 3)
// 10:  65536 Hz  -> bit 5  (counter bit 5)
// 11:  16384 Hz  -> bit 7  (counter bit 7)
static const int timer_bits[4] = { 9, 3, 5, 7 };

void timer_tick(timer_t *timer, mem_t *mem, int cycles)
{
    for (int i = 0; i < cycles; i++) {
        // DIV is always counting
        u16 prev_div = timer->div_counter;
        timer->div_counter++;

        // TIMA update when TAC bit 2 is set
        if (timer->tac & 0x04) {
            int bit = timer_bits[timer->tac & 0x03];
            bool prev = (prev_div >> bit) & 1;
            bool cur  = (timer->div_counter >> bit) & 1;

            // Falling edge detection
            if (prev && !cur) {
                timer->tima++;
                if (timer->tima == 0) {
                    // Overflow: reload from TMA and request interrupt
                    timer->tima = timer->tma;
                    interrupt_request(mem, INT_TIMER);
                }
            }
        }

        // DIV register ($FF04) = upper byte of div_counter
        mem->io[0x04] = timer->div_counter >> 8;
    }
}
