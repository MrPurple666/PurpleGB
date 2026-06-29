#ifndef APU_H
#define APU_H

#include "memory.h"
#include <SDL3/SDL.h>

#define SAMPLE_RATE      44100
#define T_CYCLES_PER_SEC 4194304

typedef struct {
    // Channel 1: square + sweep
    struct {
        u8 nr10, nr11, nr12, nr13, nr14;
        int freq;
        int duty_step;
        int length;
        int length_max;
        float volume;
        float vol_initial;
        int env_dir;    // 1=up, -1=down
        int env_period;
        int env_step;
        int sweep_period;
        int sweep_step;
        int sweep_dir;
        int sweep_shift;
        int sweep_shadow;
        bool sweep_enabled;
        bool active;
    } ch1;

    // Channel 2: square
    struct {
        u8 nr21, nr22, nr23, nr24;
        int freq;
        int duty_step;
        int length;
        int length_max;
        float volume;
        float vol_initial;
        int env_dir;
        int env_period;
        int env_step;
        bool active;
    } ch2;

    // Channel 3: wave
    struct {
        u8 nr30, nr31, nr32, nr33, nr34;
        u8 wave_ram[16];
        int freq;
        int length;
        int length_max;
        int sample_index;
        float volume;
        bool active;
    } ch3;

    // Channel 4: noise
    struct {
        u8 nr41, nr42, nr43, nr44;
        u16 lfsr;
        int length;
        int length_max;
        float volume;
        float vol_initial;
        int env_dir;
        int env_period;
        int env_step;
        bool active;
    } ch4;

    // Frame sequencer
    int frame_seq;
    int frame_seq_cycles;

    // Master controls
    bool enabled;
    u8 nr50, nr51;

    SDL_AudioStream *stream;
    u64 total_cycles;
} apu_t;

void apu_init(apu_t *apu);
void apu_write_reg(apu_t *apu, u8 reg, u8 val);
u8   apu_read_reg(apu_t *apu, u8 reg);
void apu_tick(apu_t *apu, int cycles);
void apu_generate(apu_t *apu, s16 *buf, int samples);

#endif
