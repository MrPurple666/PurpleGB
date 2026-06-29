#include "apu.h"
#include "dbg.h"
#include <string.h>
#include <stdlib.h>

/* Duty cycle tables: 0=12.5%, 1=25%, 2=50%, 3=75% */
static const u8 duty_table[4][8] = {
    {0,0,0,0,0,0,0,1}, /* 12.5% */
    {1,0,0,0,0,0,0,1}, /* 25% */
    {1,0,0,0,0,1,1,1}, /* 50% */
    {0,1,1,1,1,1,1,0}, /* 75% */
};

/* Length table */
static const u16 length_table[4] = {64, 256, 16 * 2, 4 * 2};

static void channel_tick_length(channel_t *ch) {
    if (ch->length_enabled && ch->length_counter > 0) {
        ch->length_counter--;
        if (ch->length_counter == 0)
            ch->enabled = false;
    }
}

static void channel_tick_envelope(channel_t *ch) {
    if (ch->envelope_period == 0) return;
    if (ch->envelope_timer > 0)
        ch->envelope_timer--;
    if (ch->envelope_timer == 0) {
        ch->envelope_timer = ch->envelope_period;
        if (ch->envelope_dir && ch->volume < 15)
            ch->volume++;
        else if (!ch->envelope_dir && ch->volume > 0)
            ch->volume--;
    }
}

static void sweep_calculate_freq(channel_t *ch) {
    u16 new_freq = ch->sweep_freq >> ch->sweep_shift;
    if (ch->sweep_negate)
        new_freq = ch->sweep_freq - new_freq;
    else
        new_freq = ch->sweep_freq + new_freq;
    ch->freq = new_freq & 0x7FF;
}

static void sweep_tick(channel_t *ch) {
    if (ch->sweep_period == 0) return;
    if (ch->sweep_timer > 0)
        ch->sweep_timer--;
    if (ch->sweep_timer == 0) {
        ch->sweep_timer = ch->sweep_period;
        if (ch->sweep_period > 0 && ch->enabled && ch->length_counter > 0) {
            sweep_calculate_freq(ch);
            /* Check overflow */
            if (ch->freq > 0x7FF)
                ch->enabled = false;
        }
    }
}

static void channel_tick_square(channel_t *ch) {
    if (ch->freq_timer > 0)
        ch->freq_timer--;
    if (ch->freq_timer == 0) {
        u16 p = ch->freq ? ch->freq : 8;
        ch->freq_timer = (2048 - p) * 4;
        ch->duty_pos = (ch->duty_pos + 1) & 7;
    }
}

static void channel_tick_wave(channel_t *ch) {
    if (ch->freq_timer > 0)
        ch->freq_timer--;
    if (ch->freq_timer == 0) {
        u16 p = ch->freq ? ch->freq : 8;
        ch->freq_timer = (2048 - p) * 2;
        ch->wave_pos = (ch->wave_pos + 1) & 31;
    }
}

static void channel_tick_noise(channel_t *ch) {
    if (ch->freq_timer > 0)
        ch->freq_timer--;
    if (ch->freq_timer == 0) {
        static const int divisors[8] = {8, 16, 32, 48, 64, 80, 96, 112};
        int shift = (ch->freq >> 4) & 0xF;
        if (shift >= 14) return; /* clocking stopped */
        ch->freq_timer = divisors[ch->freq & 7] << shift;

        /* Tick LFSR */
        u8 xor_val = (ch->lfsr & 1) ^ ((ch->lfsr >> 1) & 1);
        ch->lfsr = (ch->lfsr >> 1) | (xor_val << 14);
        if (ch->lfsr_width_7) {
            ch->lfsr &= ~(1 << 6);
            ch->lfsr |= (xor_val << 6);
        }
    }
}

static u8 get_square_output(channel_t *ch) {
    if (!ch->enabled || !ch->dac_enabled || ch->length_counter == 0)
        return 0;
    u8 pos = (ch->duty_pos) & 7;
    return duty_table[ch->duty][pos] ? ch->volume : 0;
}


static u8 get_noise_output(channel_t *ch) {
    if (!ch->enabled || !ch->dac_enabled || ch->length_counter == 0)
        return 0;
    return (~ch->lfsr & 1) ? ch->volume : 0;
}

static void frame_seq_step(apu_t *apu) {
    switch (apu->frame_seq) {
        case 0: /* 1, 2, 3, 4: length */
            channel_tick_length(&apu->ch1);
            channel_tick_length(&apu->ch2);
            channel_tick_length(&apu->ch3);
            channel_tick_length(&apu->ch4);
            break;
        case 2: /* 2, 6: sweep + length */
            channel_tick_length(&apu->ch1);
            channel_tick_length(&apu->ch2);
            channel_tick_length(&apu->ch3);
            channel_tick_length(&apu->ch4);
            sweep_tick(&apu->ch1);
            break;
        case 4: /* 4: length */
            channel_tick_length(&apu->ch1);
            channel_tick_length(&apu->ch2);
            channel_tick_length(&apu->ch3);
            channel_tick_length(&apu->ch4);
            break;
        case 6: /* 6: sweep + length */
            channel_tick_length(&apu->ch1);
            channel_tick_length(&apu->ch2);
            channel_tick_length(&apu->ch3);
            channel_tick_length(&apu->ch4);
            sweep_tick(&apu->ch1);
            break;
        case 7: /* 7: volume envelope (all channels) */
            channel_tick_envelope(&apu->ch1);
            channel_tick_envelope(&apu->ch2);
            channel_tick_envelope(&apu->ch4);
            break;
    }
}

void apu_init(apu_t *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->ch1.lfsr = 0x7FFF;
    apu->ch2.lfsr = 0x7FFF;
    apu->ch3.lfsr = 0x7FFF;
    apu->ch4.lfsr = 0x7FFF;
    apu->sample_rate = 44100;
    apu->sample_capacity = 8192;
    apu->sample_buf = (s16 *)calloc(1, apu->sample_capacity * sizeof(s16));
    apu->sample_pos = 0;
    apu->frame_seq = 0;
    apu->frame_seq_timer = 0;
    apu->nr52 = 0;
    apu->nr50 = 0;
    apu->nr51 = 0;
    apu->initialized = true;
    DBG(APU, "APU init");
}

void apu_cleanup(apu_t *apu) {
    if (apu->sample_buf) {
        free(apu->sample_buf);
        apu->sample_buf = NULL;
    }
    apu->initialized = false;
}

void apu_tick(apu_t *apu, int cycles) {
    if (!(apu->nr52 & 0x80)) return;

    for (int i = 0; i < cycles; i++) {
        /* Frame sequencer: runs at 8192 Hz = 1/512 Hz period */
        apu->frame_seq_timer++;
        if (apu->frame_seq_timer >= 512) {
            apu->frame_seq_timer = 0;
            frame_seq_step(apu);
            apu->frame_seq = (apu->frame_seq + 1) & 7;
        }

        /* Channel period timers */
        channel_tick_square(&apu->ch1);
        channel_tick_square(&apu->ch2);
        channel_tick_wave(&apu->ch3);
        channel_tick_noise(&apu->ch4);

        /* Mix and output at sample_rate */
        /* Approximate: we accumulate at T-cycle rate, then downsample */
        /* For simplicity, we'll output one sample every (4194304/sample_rate) ≈ 95 T-cycles */
        static int sample_accum = 0;
        static const int sample_period = 95; /* ~44100 Hz at 4.194 MHz */
        sample_accum++;
        if (sample_accum >= sample_period) {
            sample_accum = 0;
            /* Mix all channels */
            u8 ch1_out = get_square_output(&apu->ch1);
            u8 ch2_out = get_square_output(&apu->ch2);
            u8 ch3_out = 0; /* get_wave_output needs apu context */
            u8 ch4_out = get_noise_output(&apu->ch4);

            /* CH3 wave output */
            if (apu->ch3.enabled && apu->ch3.length_counter > 0) {
                u32 pos = apu->ch3.wave_pos;
                u8 shift = (pos & 1) ? 0 : 4;
                u8 sample = (apu->wave_ram.wave[pos >> 1] >> shift) & 0x0F;

                /* Output level from NR32 */
                static const u8 wave_levels[4] = {4, 0, 1, 2}; /* mute, 100%, 50%, 25% */
                u8 level_shift = wave_levels[(apu->ch3.nr32_level >> 5) & 3];
                ch3_out = sample >> level_shift;
            }

            /* Apply panning and volume */
            /* Left output: mix of channels based on NR51 bits 4-7 */
            /* Right output: mix of channels based on NR51 bits 0-3 */
            u8 left_vol = (apu->nr50 >> 4) & 7;
            u8 right_vol = apu->nr50 & 7;

            s16 left = 0, right = 0;
            if (apu->nr51 & 0x10) left += ch1_out;
            if (apu->nr51 & 0x20) left += ch2_out;
            if (apu->nr51 & 0x40) left += ch3_out;
            if (apu->nr51 & 0x80) left += ch4_out;
            if (apu->nr51 & 0x01) right += ch1_out;
            if (apu->nr51 & 0x02) right += ch2_out;
            if (apu->nr51 & 0x04) right += ch3_out;
            if (apu->nr51 & 0x08) right += ch4_out;

            left = (left * (left_vol + 1) * 256) / 60;
            right = (right * (right_vol + 1) * 256) / 60;

            if (apu->sample_pos < apu->sample_capacity) {
                apu->sample_buf[apu->sample_pos++] = left;
                apu->sample_buf[apu->sample_pos++] = right;
            }
        }
    }
}

int apu_get_samples(apu_t *apu, s16 *out, int max) {
    int avail = apu->sample_pos;
    if (avail > max) avail = max;
    memcpy(out, apu->sample_buf, avail * sizeof(s16));
    memmove(apu->sample_buf, apu->sample_buf + avail,
            (apu->sample_pos - avail) * sizeof(s16));
    apu->sample_pos -= avail;
    return avail;
}

/* Register read */
u8 apu_read(apu_t *apu, u16 addr) {
    if (!(apu->nr52 & 0x80)) {
        /* When APU is off, all registers read as 0 (except NR52) */
        if (addr == 0xFF26) return apu->nr52;
        if (addr >= 0xFF30 && addr <= 0xFF3F) return 0xFF;
        return 0xFF;
    }

    switch (addr) {
        case 0xFF10: /* NR10 - Sweep */
            return 0x80 | (apu->ch1.sweep_period << 4) |
                   (apu->ch1.sweep_negate ? 0x08 : 0) | apu->ch1.sweep_shift;
        case 0xFF11: /* NR11 - Duty/Length */
            return 0x3F | (apu->ch1.duty << 6);
        case 0xFF12: /* NR12 - Volume/Envelope */
            return (apu->ch1.volume_init << 4) |
                   (apu->ch1.envelope_dir ? 0x08 : 0) | apu->ch1.envelope_period;
        case 0xFF14: /* NR14 - Frequency high/Control */
            return 0xBF | (apu->ch1.length_enabled ? 0x40 : 0);
        case 0xFF16: /* NR21 - Duty/Length */
            return 0x3F | (apu->ch2.duty << 6);
        case 0xFF17: /* NR22 - Volume/Envelope */
            return (apu->ch2.volume_init << 4) |
                   (apu->ch2.envelope_dir ? 0x08 : 0) | apu->ch2.envelope_period;
        case 0xFF19: /* NR24 - Frequency high/Control */
            return 0xBF | (apu->ch2.length_enabled ? 0x40 : 0);
        case 0xFF1A: /* NR30 - DAC Enable */
            return 0x7F | (apu->ch3.dac_enabled ? 0x80 : 0);
        case 0xFF1C: /* NR32 - Output level */
            return 0x9F; /* Bits 3-5 readable */
        case 0xFF1E: /* NR34 - Frequency high/Control */
            return 0xBF | (apu->ch3.length_enabled ? 0x40 : 0);
        case 0xFF20: /* NR41 - Length (write-only) */
            return 0xFF;
        case 0xFF22: /* NR43 - Polynomial counter */
            return (apu->ch4.lfsr_width_7 ? 0x08 : 0) |
                   (apu->ch4.freq & 0x07);
        case 0xFF23: /* NR44 - Control */
            return 0xBF | (apu->ch4.length_enabled ? 0x40 : 0);
        case 0xFF24: /* NR50 */
            return apu->nr50;
        case 0xFF25: /* NR51 */
            return apu->nr51;
        case 0xFF26: /* NR52 - Status */
            {
                u8 status = apu->nr52 & 0x80;
                status |= (apu->ch1.enabled ? 0x01 : 0) |
                          (apu->ch2.enabled ? 0x02 : 0) |
                          (apu->ch3.enabled ? 0x04 : 0) |
                          (apu->ch4.enabled ? 0x08 : 0);
                status |= 0x70; /* Unused bits read as 1 */
                return status;
            }
        default:
            if (addr >= 0xFF30 && addr <= 0xFF3F) {
                return apu->wave_ram.wave[addr - 0xFF30];
            }
            return 0xFF;
    }
}

/* Register write */
void apu_write(apu_t *apu, u16 addr, u8 val) {
    /* If APU is off, only NR52 is writable */
    if (!(apu->nr52 & 0x80) && addr != 0xFF26) {
        if (addr >= 0xFF30 && addr <= 0xFF3F)
            apu->wave_ram.wave[addr - 0xFF30] = val;
        return;
    }

    switch (addr) {
        case 0xFF10: /* NR10 - Sweep */
            apu->ch1.sweep_period = (val >> 4) & 7;
            apu->ch1.sweep_negate = (val & 0x08) != 0;
            apu->ch1.sweep_shift = val & 7;
            break;
        case 0xFF11: /* NR11 - Duty/Length */
            apu->ch1.duty = (val >> 6) & 3;
            apu->ch1.length_counter = length_table[apu->ch1.duty] - (val & 0x3F);
            break;
        case 0xFF12: /* NR12 - Volume/Envelope */
            apu->ch1.volume_init = (val >> 4) & 0x0F;
            apu->ch1.envelope_dir = (val & 0x08) != 0;
            apu->ch1.envelope_period = val & 7;
            /* DAC disabled if bits 3-7 are all 0 */
            apu->ch1.dac_enabled = (val & 0xF8) != 0;
            if (!apu->ch1.dac_enabled)
                apu->ch1.enabled = false;
            break;
        case 0xFF13: /* NR13 - Frequency low (write-only) */
            apu->ch1.freq = (apu->ch1.freq & 0x700) | val;
            break;
        case 0xFF14: /* NR14 - Frequency high/Control */
            apu->ch1.freq = (apu->ch1.freq & 0xFF) | ((val & 7) << 8);
            apu->ch1.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) {
                /* Trigger */
                apu->ch1.enabled = true;
                if (apu->ch1.length_counter == 0)
                    apu->ch1.length_counter = 64;
                apu->ch1.volume = apu->ch1.volume_init;
                apu->ch1.duty_pos = 0;
                apu->ch1.freq_timer = (2048 - apu->ch1.freq) * 4;
                apu->ch1.sweep_freq = apu->ch1.freq;
                apu->ch1.sweep_timer = apu->ch1.sweep_period;
                if (apu->ch1.sweep_period > 0 || apu->ch1.sweep_shift > 0)
                    sweep_calculate_freq(&apu->ch1);
                if (apu->ch1.sweep_shift > 0 && apu->ch1.freq > 0x7FF)
                    apu->ch1.enabled = false;
            }
            break;
        case 0xFF16: /* NR21 - Duty/Length */
            apu->ch2.duty = (val >> 6) & 3;
            apu->ch2.length_counter = 64 - (val & 0x3F);
            break;
        case 0xFF17: /* NR22 - Volume/Envelope */
            apu->ch2.volume_init = (val >> 4) & 0x0F;
            apu->ch2.envelope_dir = (val & 0x08) != 0;
            apu->ch2.envelope_period = val & 7;
            apu->ch2.dac_enabled = (val & 0xF8) != 0;
            if (!apu->ch2.dac_enabled)
                apu->ch2.enabled = false;
            break;
        case 0xFF18: /* NR23 - Frequency low */
            apu->ch2.freq = (apu->ch2.freq & 0x700) | val;
            break;
        case 0xFF19: /* NR24 - Frequency high/Control */
            apu->ch2.freq = (apu->ch2.freq & 0xFF) | ((val & 7) << 8);
            apu->ch2.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) {
                apu->ch2.enabled = true;
                if (apu->ch2.length_counter == 0)
                    apu->ch2.length_counter = 64;
                apu->ch2.volume = apu->ch2.volume_init;
                apu->ch2.duty_pos = 0;
                apu->ch2.freq_timer = (2048 - apu->ch2.freq) * 4;
            }
            break;
        case 0xFF1A: /* NR30 - DAC Enable */
            apu->ch3.dac_enabled = (val & 0x80) != 0;
            if (!apu->ch3.dac_enabled)
                apu->ch3.enabled = false;
            break;
        case 0xFF1B: /* NR31 - Length */
            apu->ch3.length_counter = 256 - val;
            break;
        case 0xFF1C: /* NR32 - Output level */
            apu->ch3.nr32_level = val;
            break;
        case 0xFF1D: /* NR33 - Frequency low */
            apu->ch3.freq = (apu->ch3.freq & 0x700) | val;
            break;
        case 0xFF1E: /* NR34 - Frequency high/Control */
            apu->ch3.freq = (apu->ch3.freq & 0xFF) | ((val & 7) << 8);
            apu->ch3.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) {
                apu->ch3.enabled = true;
                if (apu->ch3.length_counter == 0)
                    apu->ch3.length_counter = 256;
                apu->ch3.wave_pos = 0;
                apu->ch3.freq_timer = (2048 - apu->ch3.freq) * 2;
            }
            break;
        case 0xFF20: /* NR41 - Length (write-only) */
            apu->ch4.length_counter = 64 - (val & 0x3F);
            break;
        case 0xFF21: /* NR42 - Volume/Envelope */
            apu->ch4.volume_init = (val >> 4) & 0x0F;
            apu->ch4.envelope_dir = (val & 0x08) != 0;
            apu->ch4.envelope_period = val & 7;
            apu->ch4.dac_enabled = (val & 0xF8) != 0;
            if (!apu->ch4.dac_enabled)
                apu->ch4.enabled = false;
            break;
        case 0xFF22: /* NR43 - Polynomial counter */
            apu->ch4.freq = val & 7;
            apu->ch4.lfsr_width_7 = (val & 0x08) != 0;
            /* Shift frequency bits */
            apu->ch4.freq |= (val & 0xF0);
            break;
        case 0xFF23: /* NR44 - Control */
            apu->ch4.length_enabled = (val & 0x40) != 0;
            if (val & 0x80) {
                apu->ch4.enabled = true;
                if (apu->ch4.length_counter == 0)
                    apu->ch4.length_counter = 64;
                apu->ch4.volume = apu->ch4.volume_init;
                apu->ch4.lfsr = 0x7FFF; /* reset LFSR to all 1s */
                apu->ch4.freq_timer = 0; /* immediately start */
            }
            break;
        case 0xFF24: /* NR50 */
            apu->nr50 = val;
            break;
        case 0xFF25: /* NR51 */
            apu->nr51 = val;
            break;
        case 0xFF26: /* NR52 - Master enable */
            if (!(val & 0x80)) {
                /* APU off: reset all registers */
                apu->nr50 = 0;
                apu->nr51 = 0;
                memset(&apu->ch1, 0, sizeof(channel_t));
                memset(&apu->ch2, 0, sizeof(channel_t));
                memset(&apu->ch3, 0, sizeof(channel_t));
                memset(&apu->ch4, 0, sizeof(channel_t));
                apu->ch1.lfsr = 0x7FFF;
                apu->ch2.lfsr = 0x7FFF;
                apu->ch3.lfsr = 0x7FFF;
                apu->ch4.lfsr = 0x7FFF;
                apu->nr52 = 0;
                apu->frame_seq = 0;
                apu->frame_seq_timer = 0;
            } else {
                apu->nr52 = 0x80;
            }
            break;
        default:
            if (addr >= 0xFF30 && addr <= 0xFF3F) {
                apu->wave_ram.wave[addr - 0xFF30] = val;
            }
            break;
    }
}
