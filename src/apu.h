#ifndef APU_H
#define APU_H

#include "memory.h"
#include <SDL3/SDL.h>

/* Sound channel types */
typedef struct {
    /* Length counter (shared by all channels) */
    u16 length_counter;
    bool length_enabled;

    /* Envelope (CH1, CH2, CH4) */
    u8 volume;
    u8 volume_init;
    bool envelope_dir;  /* true = increase */
    u8 envelope_period;
    u8 envelope_timer;

    /* Sweep (CH1 only) */
    u8 sweep_period;
    u8 sweep_timer;
    bool sweep_negate;
    u8 sweep_shift;
    u16 sweep_freq;     /* shadow frequency */

    /* Duty / waveform */
    u8 duty;
    u8 duty_pos;

    /* Frequency / period */
    u16 freq;           /* 11-bit frequency */
    u16 freq_timer;

    /* DAC */
    bool dac_enabled;
    bool enabled;

    u16 lfsr;            /* CH4: 15-bit LFSR */
    bool lfsr_width_7;  /* CH4: 7-bit mode */
    u32 wave_pos;       /* CH3: position in wave RAM */
    u8 nr32_level;      /* CH3: output level (0-3) from NR32 bits 5-6 */
} channel_t;

/* Wave pattern RAM */
typedef struct {
    u8 wave[16];        /* 16 bytes = 32 4-bit samples */
} wave_ram_t;

typedef struct {
    /* 4 channels */
    channel_t ch1, ch2, ch3, ch4;

    /* Wave RAM */
    wave_ram_t wave_ram;

    /* Master */
    u8 nr50;            /* FF24: volume */
    u8 nr51;            /* FF25: panning */
    u8 nr52;            /* FF26: master enable */

    /* Frame sequencer */
    u8 frame_seq;       /* 0-7 */
    int frame_seq_timer;

    /* Output */
    s16 *sample_buf;
    int sample_pos;
    int sample_capacity;

    /* Audio specs */
    int sample_rate;
    bool initialized;
} apu_t;

void apu_init(apu_t *apu);
void apu_tick(apu_t *apu, int cycles);
u8   apu_read(apu_t *apu, u16 addr);
void apu_write(apu_t *apu, u16 addr, u8 val);
int  apu_get_samples(apu_t *apu, s16 *out, int max);
void apu_cleanup(apu_t *apu);

#endif
