#ifndef JOYPAD_H
#define JOYPAD_H

#include "memory.h"
#include <SDL3/SDL.h>

typedef struct {
    bool select_buttons;
    bool select_dpad;
    bool button_a, button_b, button_select, button_start;
    bool right, left, up, down;
} joypad_t;

void joypad_init(joypad_t *joypad);
void joypad_handle_event(joypad_t *joypad, mem_t *mem, SDL_Event *e);
u8   joypad_read(joypad_t *joypad);

#endif
