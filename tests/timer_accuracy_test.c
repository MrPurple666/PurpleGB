#include "../src/memory.h"
#include "../src/timer.h"
#include "../src/interrupt.h"
#include <assert.h>
#include <string.h>

static void setup(mem_t *mem, timer_t *timer)
{
    mem_init(mem);
    timer_init(timer);
    mem->timer = timer;
    mem->io[0x0F] = 0;
    mem->ie = 0;
}

static void test_div_write_resets_visible_div(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    timer_tick(&timer, &mem, 300);
    assert(mem.io[0x04] != 0);

    mem_write(&mem, 0xFF04, 0xAB);
    assert(timer.div_counter == 0);
    assert(mem.io[0x04] == 0);
}

static void test_timer_disabled_does_not_increment_tima(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    mem.io[0x05] = 0x22;
    mem.io[0x07] = 0x00;
    timer_tick(&timer, &mem, 1024);
    assert(mem.io[0x05] == 0x22);
}

static void test_timer_enabled_increments_on_selected_edge(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    mem.io[0x05] = 0x00;
    mem.io[0x07] = 0x05; /* enable, input clock select 01 => div bit 3 */

    timer_tick(&timer, &mem, 16);
    assert(mem.io[0x05] == 1);

    timer_tick(&timer, &mem, 16);
    assert(mem.io[0x05] == 2);
}

static void test_timer_overflow_reloads_tma_and_requests_interrupt(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    mem.io[0x05] = 0xFF;
    mem.io[0x06] = 0x9C;
    mem.io[0x07] = 0x05;

    timer_tick(&timer, &mem, 16);
    timer_tick(&timer, &mem, 4);

    assert(mem.io[0x05] == 0x9C);
    assert((mem.io[0x0F] & (1 << INT_TIMER)) != 0);
}

static void test_div_write_can_trigger_falling_edge_increment(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    mem.io[0x05] = 0x10;
    mem.io[0x07] = 0x05;

    timer.div_counter = 0x0008; /* selected bit high before DIV reset */
    mem.io[0x04] = timer.div_counter >> 8;

    mem_write(&mem, 0xFF04, 0x00);
    assert(mem.io[0x05] == 0x11);
}

static void test_tac_write_can_trigger_falling_edge_increment(void)
{
    mem_t mem;
    timer_t timer;
    setup(&mem, &timer);

    mem.io[0x05] = 0x20;
    mem.io[0x07] = 0x05;
    timer.div_counter = 0x0008; /* old selected bit high, disable causes falling edge */

    mem_write(&mem, 0xFF07, 0x00);
    assert(mem.io[0x05] == 0x21);
}

int main(void)
{
    test_div_write_resets_visible_div();
    test_timer_disabled_does_not_increment_tima();
    test_timer_enabled_increments_on_selected_edge();
    test_timer_overflow_reloads_tma_and_requests_interrupt();
    test_div_write_can_trigger_falling_edge_increment();
    test_tac_write_can_trigger_falling_edge_increment();
    return 0;
}
