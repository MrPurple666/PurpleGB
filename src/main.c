#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string.h>

#include "memory.h"
#include "cpu.h"
#include "interrupt.h"
#include "timer.h"
#include "joypad.h"

#define LCD_WIDTH  160
#define LCD_HEIGHT 144

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    cpu_init_opcodes();

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;

    SDL_Window *window = SDL_CreateWindow("PurpleGB", 480, 432, SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); return 1; }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, LCD_WIDTH, LCD_HEIGHT);
    if (!texture) { SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    mem_t mem;
    cpu_t cpu;
    timer_t timer;
    joypad_t joypad;
    mem_init(&mem);
    cpu_init(&cpu);
    timer_init(&timer);
    joypad_init(&joypad);

    u32 framebuffer[LCD_WIDTH * LCD_HEIGHT];
    memset(framebuffer, 0xFF, sizeof(framebuffer));

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            joypad_handle_event(&joypad, &mem, &e);
        }

        for (int i = 0; i < 1000 && !cpu.halted && !cpu.stopped; i++) {
            int cyc = cpu_step(&cpu, &mem);
            timer_tick(&timer, &mem, cyc);
        }

        SDL_UpdateTexture(texture, NULL, framebuffer, LCD_WIDTH * sizeof(u32));
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
