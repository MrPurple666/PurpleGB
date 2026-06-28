#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string.h>

#include "memory.h"
#include "cpu.h"
#include "interrupt.h"
#include "timer.h"
#include "joypad.h"
#include "ppu.h"
#include "apu.h"

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    cpu_init_opcodes();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) return 1;

    SDL_Window *window = SDL_CreateWindow("PurpleGB", 480, 432, SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); return 1; }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
    if (!renderer) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING, 160, 144);
    if (!texture) { SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit(); return 1; }

    mem_t mem;
    cpu_t cpu;
    timer_t timer;
    joypad_t joypad;
    ppu_t ppu;
    apu_t apu;
    mem_init(&mem);
    cpu_init(&cpu);
    timer_init(&timer);
    joypad_init(&joypad);
    ppu_init(&ppu);
    apu_init(&apu);

    SDL_AudioSpec aspec = {0};
    aspec.format = SDL_AUDIO_S16;
    aspec.channels = 2;
    aspec.freq = SAMPLE_RATE;
    SDL_AudioStream *audio_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &aspec, NULL, NULL);
    if (audio_stream)
        SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(audio_stream));

    s16 audio_buf[512];

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit = true;
            joypad_handle_event(&joypad, &mem, &e);
        }

        int total = 0;
        while (total < 70224 && !cpu.halted && !cpu.stopped) {
            int cyc = cpu_step(&cpu, &mem);
            ppu_tick(&ppu, &mem, cyc);
            timer_tick(&timer, &mem, cyc);
            apu_tick(&apu, cyc);
            total += cyc;
        }

        int samples = (70224 * SAMPLE_RATE) / T_CYCLES_PER_SEC;
        if (samples > 512) samples = 512;
        apu_generate(&apu, audio_buf, samples);
        if (audio_stream)
            SDL_PutAudioStreamData(audio_stream, audio_buf, samples * sizeof(s16) * 2);

        SDL_UpdateTexture(texture, NULL, ppu.framebuffer, 160 * sizeof(u32));
        SDL_RenderTexture(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (audio_stream) SDL_DestroyAudioStream(audio_stream);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
