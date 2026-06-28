#include "joypad.h"
#include "interrupt.h"

void joypad_init(joypad_t *joypad)
{
    joypad->select_buttons = false;
    joypad->select_dpad = false;
    joypad->button_a = joypad->button_b =
    joypad->button_select = joypad->button_start = false;
    joypad->right = joypad->left =
    joypad->up = joypad->down = false;
}

void joypad_handle_event(joypad_t *joypad, mem_t *mem, SDL_Event *e)
{
    bool pressed;
    SDL_Keycode key;

    if (e->type == SDL_EVENT_KEY_DOWN) {
        pressed = true;
        key = e->key.key;
    } else if (e->type == SDL_EVENT_KEY_UP) {
        pressed = false;
        key = e->key.key;
    } else {
        return;
    }

    bool old_buttons = joypad->button_a | joypad->button_b |
                       joypad->button_select | joypad->button_start;
    bool old_dpad    = joypad->right | joypad->left |
                       joypad->up | joypad->down;

    switch (key) {
        case SDLK_Z: joypad->button_a = pressed; break;
        case SDLK_X: joypad->button_b = pressed; break;
        case SDLK_RETURN:  joypad->button_start = pressed; break;
        case SDLK_TAB:    joypad->button_select = pressed; break;
        case SDLK_RIGHT: joypad->right = pressed; break;
        case SDLK_LEFT:  joypad->left  = pressed; break;
        case SDLK_UP:    joypad->up    = pressed; break;
        case SDLK_DOWN:  joypad->down  = pressed; break;
    }

    // Request joypad interrupt on key-down if the corresponding column is selected
    if (pressed) {
        bool new_buttons = joypad->button_a | joypad->button_b |
                           joypad->button_select | joypad->button_start;
        bool new_dpad    = joypad->right | joypad->left |
                           joypad->up | joypad->down;
        if ((joypad->select_buttons && new_buttons && !old_buttons) ||
            (joypad->select_dpad && new_dpad && !old_dpad)) {
            interrupt_request(mem, INT_JOYPAD);
        }
    }
}

u8 joypad_read(joypad_t *joypad)
{
    u8 val = 0xCF; // bits 7-6 = 11, bit 5 = 1 (deselected), bit 4 = 1 (deselected)

    if (joypad->select_buttons) {
        val &= ~0x20; // bit 5 = 0 indicates buttons selected
        if (!joypad->button_a)      val &= ~0x01;
        if (!joypad->button_b)      val &= ~0x02;
        if (!joypad->button_select) val &= ~0x04;
        if (!joypad->button_start)  val &= ~0x08;
    }

    if (joypad->select_dpad) {
        val &= ~0x10; // bit 4 = 0 indicates dpad selected
        if (!joypad->right) val &= ~0x01;
        if (!joypad->left)  val &= ~0x02;
        if (!joypad->up)    val &= ~0x04;
        if (!joypad->down)  val &= ~0x08;
    }

    return val;
}
