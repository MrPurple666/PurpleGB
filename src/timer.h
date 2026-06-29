#ifndef TIMER_H
#define TIMER_H

#include "memory.h"

typedef struct {
    u16 div_counter;
} timer_t;

void timer_init(timer_t *timer);
void timer_tick(timer_t *timer, mem_t *mem, int cycles);
void timer_write_div(timer_t *timer, mem_t *mem);
void timer_write_tac(timer_t *timer, mem_t *mem, u8 value);

#endif
