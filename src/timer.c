#include "timer.h"
#include "interrupt.h"

static const int timer_bits[4] = { 9, 3, 5, 7 };

static bool timer_signal(const timer_t *timer, const mem_t *mem)
{
    u8 tac = mem->io[0x07];
    if ((tac & 0x04) == 0) {
        return false;
    }

    int bit = timer_bits[tac & 0x03];
    return ((timer->div_counter >> bit) & 1) != 0;
}

static void timer_increment_tima(mem_t *mem)
{
    u8 tima = (u8)(mem->io[0x05] + 1);
    if (tima == 0) {
        mem->io[0x05] = mem->io[0x06];
        interrupt_request(mem, INT_TIMER);
        return;
    }

    mem->io[0x05] = tima;
}

static void timer_apply_falling_edge(mem_t *mem, bool old_signal, bool new_signal)
{
    if (old_signal && !new_signal) {
        timer_increment_tima(mem);
    }
}

void timer_init(timer_t *timer)
{
    timer->div_counter = 0;
}

void timer_write_div(timer_t *timer, mem_t *mem)
{
    bool old_signal = timer_signal(timer, mem);

    timer->div_counter = 0;
    mem->io[0x04] = 0;

    timer_apply_falling_edge(mem, old_signal, timer_signal(timer, mem));
}

void timer_write_tac(timer_t *timer, mem_t *mem, u8 value)
{
    bool old_signal = timer_signal(timer, mem);

    mem->io[0x07] = value & 0x07;

    timer_apply_falling_edge(mem, old_signal, timer_signal(timer, mem));
}

void timer_tick(timer_t *timer, mem_t *mem, int cycles)
{
    for (int i = 0; i < cycles; i++) {
        bool old_signal = timer_signal(timer, mem);

        timer->div_counter++;
        mem->io[0x04] = timer->div_counter >> 8;

        timer_apply_falling_edge(mem, old_signal, timer_signal(timer, mem));
    }
}
