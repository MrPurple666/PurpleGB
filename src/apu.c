#include "apu.h"
#include <string.h>

// Duty cycle patterns: 12.5%, 25%, 50%, 75%
static const u8 duty_waves[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 1}, // 12.5%
    {1, 0, 0, 0, 0, 0, 0, 1}, // 25%
    {1, 0, 0, 0, 0, 1, 1, 1}, // 50%
    {0, 1, 1, 1, 1, 1, 1, 0}  // 75%
};

#define NR10 0x10
#define NR11 0x11
#define NR12 0x12
#define NR13 0x13
#define NR14 0x14
#define NR21 0x16
#define NR22 0x17
#define NR23 0x18
#define NR24 0x19
#define NR30 0x1A
#define NR31 0x1B
#define NR32 0x1C
#define NR33 0x1D
#define NR34 0x1E
#define NR41 0x20
#define NR42 0x21
#define NR43 0x22
#define NR44 0x23
#define NR50 0x24
#define NR51 0x25
#define NR52 0x26

#define FREQ_CLOCK 131072  // 2^17 / 1 for simplicity (actual: 2^20 / 4 = 262144)
#define LFSR_WIDTH7 0x7F
#define LFSR_WIDTH15 0x7FFF

// Frame sequencer: 512 Hz = 8192 T-cycles per step
#define FS_CYCLES 8192

void apu_init(apu_t *apu)
{
    memset(apu, 0, sizeof(*apu));

    apu->ch1.freq = 0;
    apu->ch1.duty_step = 0;
    apu->ch1.volume = 0;
    apu->ch1.length = 0;
    apu->ch1.env_dir = -1;
    apu->ch1.env_period = 0;
    apu->ch1.env_step = 0;
    apu->ch1.sweep_period = 0;
    apu->ch1.sweep_step = 0;
    apu->ch1.active = false;

    apu->ch2.freq = 0;
    apu->ch2.duty_step = 0;
    apu->ch2.volume = 0;
    apu->ch2.length = 0;
    apu->ch2.env_dir = -1;
    apu->ch2.env_step = 0;
    apu->ch2.active = false;

    apu->ch3.freq = 0;
    apu->ch3.length = 0;
    apu->ch3.volume = 0;
    apu->ch3.sample_index = 0;
    apu->ch3.active = false;

    apu->ch4.lfsr = 0x7FFF;
    apu->ch4.volume = 0;
    apu->ch4.length = 0;
    apu->ch4.env_dir = -1;
    apu->ch4.env_step = 0;
    apu->ch4.active = false;

    apu->frame_seq = 0;
    apu->frame_seq_cycles = 0;
    apu->enabled = false;
    apu->nr50 = 0x77; // full volume both channels
    apu->nr51 = 0xF3; // all channels enabled both sides
    apu->stream = NULL;
    apu->total_cycles = 0;
}

// Channel 1: square + sweep
static void trigger_ch1(apu_t *apu)
{
    apu->ch1.active = true;
    apu->ch1.duty_step = 0;
    apu->ch1.volume = apu->ch1.vol_initial;
    apu->ch1.env_step = 0;

    if (apu->ch1.length == 0) apu->ch1.length = 64;

    // Sweep setup
    apu->ch1.sweep_shadow = apu->ch1.freq;
    apu->ch1.sweep_step = 0;
    apu->ch1.sweep_enabled = (apu->ch1.sweep_period > 0 || apu->ch1.sweep_shift > 0);
}

// Channel 2: square
static void trigger_ch2(apu_t *apu)
{
    apu->ch2.active = true;
    apu->ch2.duty_step = 0;
    apu->ch2.volume = apu->ch2.vol_initial;
    apu->ch2.env_step = 0;
    if (apu->ch2.length == 0) apu->ch2.length = 64;
}

// Channel 3: wave
static void trigger_ch3(apu_t *apu)
{
    apu->ch3.active = true;
    apu->ch3.sample_index = 0;
    if (apu->ch3.length == 0) apu->ch3.length = 256;
}

// Channel 4: noise
static void trigger_ch4(apu_t *apu)
{
    apu->ch4.active = true;
    apu->ch4.lfsr = 0x7FFF;
    apu->ch4.volume = apu->ch4.vol_initial;
    apu->ch4.env_step = 0;
    if (apu->ch4.length == 0) apu->ch4.length = 64;
}

// Sweep calculation for CH1
static int calc_sweep_freq(apu_t *apu)
{
    int freq = apu->ch1.sweep_shadow >> apu->ch1.sweep_shift;
    if (apu->ch1.sweep_dir) { // sweep up
        freq = apu->ch1.sweep_shadow - freq;
    } else {
        freq = apu->ch1.sweep_shadow + freq;
    }
    if (freq > 2047) {
        apu->ch1.active = false;
    }
    return freq;
}

void apu_write_reg(apu_t *apu, u8 reg, u8 val)
{
    (void)apu; (void)reg; (void)val;
    // Store the register
    switch (reg) {
        case NR10:
            apu->ch1.nr10 = val;
            apu->ch1.sweep_period = (val >> 4) & 0x07;
            apu->ch1.sweep_dir = (val >> 3) & 1;
            apu->ch1.sweep_shift = val & 0x07;
            break;
        case NR11:
            apu->ch1.nr11 = val;
            apu->ch1.duty_step = (val >> 6) & 3;
            apu->ch1.length = 64 - (val & 0x3F);
            break;
        case NR12:
            apu->ch1.nr12 = val;
            apu->ch1.vol_initial = (val >> 4) / 15.0f;
            apu->ch1.env_dir = (val & 0x08) ? 1 : -1;
            apu->ch1.env_period = val & 0x07;
            if ((val & 0xF8) == 0) apu->ch1.active = false;
            break;
        case NR13:
            apu->ch1.nr13 = val;
            apu->ch1.freq = (apu->ch1.freq & 0x700) | val;
            break;
        case NR14:
            apu->ch1.nr14 = val;
            apu->ch1.freq = ((val & 0x07) << 8) | (apu->ch1.freq & 0xFF);
            if (val & 0x80) { // trigger
                trigger_ch1(apu);
            }
            break;

        case NR21:
            apu->ch2.nr21 = val;
            apu->ch2.duty_step = (val >> 6) & 3;
            apu->ch2.length = 64 - (val & 0x3F);
            break;
        case NR22:
            apu->ch2.nr22 = val;
            apu->ch2.vol_initial = (val >> 4) / 15.0f;
            apu->ch2.env_dir = (val & 0x08) ? 1 : -1;
            apu->ch2.env_period = val & 0x07;
            if ((val & 0xF8) == 0) apu->ch2.active = false;
            break;
        case NR23:
            apu->ch2.nr23 = val;
            apu->ch2.freq = (apu->ch2.freq & 0x700) | val;
            break;
        case NR24:
            apu->ch2.nr24 = val;
            apu->ch2.freq = ((val & 0x07) << 8) | (apu->ch2.freq & 0xFF);
            if (val & 0x80) trigger_ch2(apu);
            break;

        case NR30:
            apu->ch3.nr30 = val;
            apu->ch3.active = apu->ch3.active && (val & 0x80);
            break;
        case NR31:
            apu->ch3.nr31 = val;
            apu->ch3.length = 256 - val;
            break;
        case NR32:
            apu->ch3.nr32 = val;
            apu->ch3.volume = (val >> 5) & 3;
            break;
        case NR33:
            apu->ch3.nr33 = val;
            apu->ch3.freq = (apu->ch3.freq & 0x700) | val;
            break;
        case NR34:
            apu->ch3.nr34 = val;
            apu->ch3.freq = ((val & 0x07) << 8) | (apu->ch3.freq & 0xFF);
            if (val & 0x80) trigger_ch3(apu);
            break;

        case NR41:
            apu->ch4.nr41 = val;
            apu->ch4.length = 64 - (val & 0x3F);
            break;
        case NR42:
            apu->ch4.nr42 = val;
            apu->ch4.vol_initial = (val >> 4) / 15.0f;
            apu->ch4.env_dir = (val & 0x08) ? 1 : -1;
            apu->ch4.env_period = val & 0x07;
            if ((val & 0xF8) == 0) apu->ch4.active = false;
            break;
        case NR43:
            apu->ch4.nr43 = val;
            break;
        case NR44:
            apu->ch4.nr44 = val;
            if (val & 0x80) trigger_ch4(apu);
            break;

        case NR50:
            apu->nr50 = val;
            break;
        case NR51:
            apu->nr51 = val;
            break;
        case NR52:
            apu->enabled = (val & 0x80) != 0;
            if (!apu->enabled) {
                // Reset all channels
                apu->ch1.active = false;
                apu->ch2.active = false;
                apu->ch3.active = false;
                apu->ch4.active = false;
            }
            break;

        // Wave RAM (ch3 waveform)
        default:
            if (reg >= 0x30 && reg <= 0x3F) {
                apu->ch3.wave_ram[reg - 0x30] = val;
            }
            break;
    }
}

u8 apu_read_reg(apu_t *apu, u8 reg)
{
    (void)apu;
    switch (reg) {
        case NR10: return apu->ch1.nr10;
        case NR11: return apu->ch1.nr11 | 0xC0;
        case NR12: return apu->ch1.nr12;
        case NR13: return 0xFF; // write-only
        case NR14: return apu->ch1.nr14 | 0xBF;
        case NR21: return apu->ch2.nr21 | 0xC0;
        case NR22: return apu->ch2.nr22;
        case NR23: return 0xFF;
        case NR24: return apu->ch2.nr24 | 0xBF;
        case NR30: return apu->ch3.nr30 | 0x7F;
        case NR31: return 0xFF;
        case NR32: return apu->ch3.nr32 | 0x9F;
        case NR33: return 0xFF;
        case NR34: return apu->ch3.nr34 | 0xBF;
        case NR41: return 0xFF;
        case NR42: return apu->ch4.nr42;
        case NR43: return apu->ch4.nr43;
        case NR44: return apu->ch4.nr44 | 0xBF;
        case NR50: return apu->nr50;
        case NR51: return apu->nr51;
        case NR52: return 0x70 | (apu->enabled ? 0x80 : 0) |
                          (apu->ch1.active ? 1 : 0) |
                          (apu->ch2.active ? 2 : 0) |
                          (apu->ch3.active ? 4 : 0) |
                          (apu->ch4.active ? 8 : 0);
        default:
            if (reg >= 0x30 && reg <= 0x3F) {
                return apu->ch3.wave_ram[reg - 0x30];
            }
            return 0xFF;
    }
}

static void frame_sequencer_tick(apu_t *apu)
{
    int step = apu->frame_seq;

    // Length counter (steps 0, 2, 4, 6)
    if ((step & 1) == 0) {
        if (apu->ch1.length > 0) { apu->ch1.length--; if (apu->ch1.length == 0) apu->ch1.active = false; }
        if (apu->ch2.length > 0) { apu->ch2.length--; if (apu->ch2.length == 0) apu->ch2.active = false; }
        if (apu->ch3.length > 0) { apu->ch3.length--; if (apu->ch3.length == 0) apu->ch3.active = false; }
        if (apu->ch4.length > 0) { apu->ch4.length--; if (apu->ch4.length == 0) apu->ch4.active = false; }
    }

    // Sweep (step 2, 4)
    if (step == 2 || step == 6) {
        if (apu->ch1.sweep_enabled && apu->ch1.sweep_period > 0) {
            apu->ch1.sweep_step++;
            if (apu->ch1.sweep_step >= apu->ch1.sweep_period) {
                apu->ch1.sweep_step = 0;
                int new_freq = calc_sweep_freq(apu);
                if (new_freq <= 2047 && apu->ch1.sweep_shift > 0) {
                    apu->ch1.sweep_shadow = new_freq;
                    apu->ch1.freq = new_freq;
                }
            }
        }
    }

    // Envelope (step 7)
    if (step == 7) {
        if (apu->ch1.env_period > 0) {
            apu->ch1.env_step++;
            if (apu->ch1.env_step >= apu->ch1.env_period) {
                apu->ch1.env_step = 0;
                float new_vol = apu->ch1.volume + apu->ch1.env_dir * (1.0f / 15.0f);
                if (new_vol >= 0 && new_vol <= 1.0f) apu->ch1.volume = new_vol;
                else apu->ch1.active = false;
            }
        }
        if (apu->ch2.env_period > 0) {
            apu->ch2.env_step++;
            if (apu->ch2.env_step >= apu->ch2.env_period) {
                apu->ch2.env_step = 0;
                float new_vol = apu->ch2.volume + apu->ch2.env_dir * (1.0f / 15.0f);
                if (new_vol >= 0 && new_vol <= 1.0f) apu->ch2.volume = new_vol;
                else apu->ch2.active = false;
            }
        }
        if (apu->ch4.env_period > 0) {
            apu->ch4.env_step++;
            if (apu->ch4.env_step >= apu->ch4.env_period) {
                apu->ch4.env_step = 0;
                float new_vol = apu->ch4.volume + apu->ch4.env_dir * (1.0f / 15.0f);
                if (new_vol >= 0 && new_vol <= 1.0f) apu->ch4.volume = new_vol;
                else apu->ch4.active = false;
            }
        }
    }

    apu->frame_seq = (step + 1) & 7;
}

void apu_tick(apu_t *apu, int cycles)
{
    if (!apu->enabled) return;
    apu->total_cycles += cycles;

    // Frame sequencer
    apu->frame_seq_cycles += cycles;
    while (apu->frame_seq_cycles >= FS_CYCLES) {
        apu->frame_seq_cycles -= FS_CYCLES;
        frame_sequencer_tick(apu);
    }
}

static float sample_ch1(apu_t *apu)
{
    if (!apu->enabled || !apu->ch1.active) return 0;
    if (apu->ch1.freq == 0) return 0;

    int period = (2048 - apu->ch1.freq) * 4;
    if (period <= 0) return 0;

    int duty = duty_waves[apu->ch1.duty_step][apu->ch1.duty_step & 7];
    return duty ? apu->ch1.volume : -apu->ch1.volume;
}

static float sample_ch2(apu_t *apu)
{
    if (!apu->enabled || !apu->ch2.active) return 0;
    if (apu->ch2.freq == 0) return 0;

    int period = (2048 - apu->ch2.freq) * 4;
    if (period <= 0) return 0;

    int duty = duty_waves[apu->ch2.duty_step][apu->ch2.duty_step & 7];
    return duty ? apu->ch2.volume : -apu->ch2.volume;
}

static float sample_ch3(apu_t *apu)
{
    if (!apu->enabled || !apu->ch3.active) return 0;
    if (!(apu->ch3.nr30 & 0x80)) return 0; // DAC off
    if (apu->ch3.freq == 0) return 0;

    int idx = apu->ch3.sample_index;
    u8 sample_byte = apu->ch3.wave_ram[idx / 2];
    u8 nibble = (idx & 1) ? (sample_byte & 0x0F) : (sample_byte >> 4);

    float vol = apu->ch3.volume;
    float amp = 0;
    switch ((int)vol) {
        case 0: amp = 0; break;        // mute
        case 1: amp = nibble / 7.5f - 1.0f; break;  // 100%
        case 2: amp = nibble / 15.0f - 0.5f; break; // 50%
        case 3: amp = nibble / 30.0f - 0.25f; break; // 25%
    }

    return amp;
}

static float sample_ch4(apu_t *apu)
{
    if (!apu->enabled || !apu->ch4.active) return 0;

    // LFSR
    u8 bit0 = apu->ch4.lfsr & 1;
    u8 bit1 = (apu->ch4.lfsr >> 1) & 1;
    u8 xor_result = bit0 ^ bit1;
    apu->ch4.lfsr = (apu->ch4.lfsr >> 1) | (xor_result << 14);

    if (apu->ch4.nr43 & 0x08) {
        // 7-bit LFSR
        apu->ch4.lfsr = (apu->ch4.lfsr & ~0x40) | (xor_result << 6);
        apu->ch4.lfsr |= 0x40; // bit 7 always 1 in 7-bit mode
    }

    return bit0 ? apu->ch4.volume : -apu->ch4.volume;
}

void apu_generate(apu_t *apu, s16 *buf, int samples)
{
    for (int i = 0; i < samples; i++) {
        // Advance channels by ~95 T-cycles per sample at 44100 Hz
        apu_tick(apu, 95);

        float ch1 = sample_ch1(apu);
        float ch2 = sample_ch2(apu);
        float ch3 = sample_ch3(apu);
        float ch4 = sample_ch4(apu);

        // Mix (sum / 4)
        float sample = (ch1 + ch2 + ch3 + ch4) * 0.25f;

        // Clamp
        if (sample > 1.0f) sample = 1.0f;
        if (sample < -1.0f) sample = -1.0f;

        // Output to stereo
        s16 out = (s16)(sample * 8000);
        buf[i * 2] = out;
        buf[i * 2 + 1] = out;
    }
}
