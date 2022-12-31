#include "WavetableOscillator.h"
#include <algorithm>

namespace Flan {
    BufferSample WavetableOscillator::get_sample(const float time_per_sample, const float pitch_wheel, const int filter_mode) {
        // Immediately skip inactive stage
        if (static_cast<envStage>(vol_env.stage) == off) {
            if (midi_key != 255) {
                schedule_kill = true;
            }
            return { static_cast<sample_t>(0.0f), static_cast<sample_t>(0.0f) };
        }

        // Update note parameters
        channel_volume = voice_params->FinalLevels.Vol;
        channel_panning = voice_params->FinalLevels.Pan;
        channel_pitch = voice_params->FinalLevels.Pitch - initial_channel_pitch;

        // Update envelopes
        vol_env.update(preset_zone.vol_env, time_per_sample, true);
        mod_env.update(preset_zone.mod_env, time_per_sample, false);

        // Update LFOs
        vib_lfo.update(preset_zone.vib_lfo, time_per_sample);
        mod_lfo.update(preset_zone.mod_lfo, time_per_sample);

        // Handle sample progression
        const float pitch_wheel_contrib = (pitch_wheel / 12.0f);
        const float channel_pitch_contrib = (channel_pitch / 1200.0f);
        const float mod_env_contrib = (((100.0f + mod_env.value) * preset_zone.mod_env_to_pitch) / (1200.f * 100.f));
        const float mod_lfo_contrib = ((mod_lfo.state * preset_zone.mod_lfo_to_pitch) / (1200.f));
        const float vib_lfo_contrib = ((vib_lfo.state * preset_zone.vib_lfo_to_pitch) / (1200.f));
        sample_position += sample_delta * powf(2.0f, pitch_wheel_contrib + channel_pitch_contrib +  mod_env_contrib + mod_lfo_contrib + vib_lfo_contrib);

        // Handle offsets
        const u32 sample_start = preset_zone.sample_start_offset;
        const u32 length = sample.length + (preset_zone.sample_end_offset - preset_zone.sample_start_offset);
        const u32 loop_start = sample.loop_start + preset_zone.sample_loop_start_offset;
        const u32 loop_end = sample.loop_end + preset_zone.sample_loop_end_offset;

        // Loop around sample loop points, to avoid floating point precision errors with high sample position values (yes, this has happened before lmao)
        if (preset_zone.loop_enable && sample_position > static_cast<float>(loop_end)) {
            sample_position -= static_cast<float>(loop_end - loop_start);
        }

        // If looping is not enabled, and sample finished playing, set channel to off
        if (!preset_zone.loop_enable) {
            if (sample_position > static_cast<float>(length)) {
                vol_env.stage = static_cast<float>(off);
                return { static_cast<sample_t>(0.0f), static_cast<sample_t>(0.0f) };
            }
        }

        // After a lot of headaches and comparing with a bunch of different SoundFont tools like Viena, FluidSynth, and
        // Fruity Soundfont Player, these are the dB to linear conversion magic numbers I've found.
        const float corrected_adsr_volume = powf(2.0f, ((vol_env.value - (mod_lfo.state * preset_zone.mod_lfo_to_volume)) / 6.0f))
                                    * powf(2.0f, (-preset_zone.init_attenuation / 15.0f));

        // Calculate stereo volume factors, and calculate the center index
        const float mul_base = corrected_adsr_volume * channel_volume;
        const float mul_l = mul_base * ((-(channel_panning) + 1.0f) / 2.0f) * ((-(preset_zone.pan) + 1.0f) / 2.0f);
        const float mul_r = mul_base * ((+(channel_panning) + 1.0f) / 2.0f) * ((+(preset_zone.pan) + 1.0f) / 2.0f);
        const int index = static_cast<int>(sample_position) + static_cast<int>(sample_start);

        float sample_data = 0;
        float sample_link = 0;
        // Point sampling (1-tap)
        if (filter_mode == 0) {
            sample_data = sample_from_index(index, false);
            sample_link = sample_from_index(index, true);
        }
        // Linear filtering (2-tap)
        else if (filter_mode == 1) {
            const float t = fmod(sample_position, 1.0f);
            const float sample_data1 = sample_from_index(index, false);
            const float sample_link1 = sample_from_index(index, true);
            const float sample_data2 = sample_from_index(index + 1, false);
            const float sample_link2 = sample_from_index(index + 1, true);
            sample_data = lerp(sample_data1, sample_data2, t);
            sample_link = lerp(sample_link1, sample_link2, t);
        }
        // Gaussian filter (4-tap)
        else if (filter_mode == 2) {
            for (int i = -1; i < 3; i++) {
                // Get sample index
                const int sample_index = index + i;

                // Get distance from sample_position
                const float distance = abs(sample_position - static_cast<float>(sample_index));

                // Index bell curve
                sample_data += sample_from_index(sample_index, false) * bell_curve[static_cast<int>(distance * 256)];
                sample_link += sample_from_index(sample_index, false) * bell_curve[static_cast<int>(distance * 256)];
            }
        }


        float sample_l, sample_r;
        switch (sample.type) {
        case leftSample:
            sample_l = sample_data * mul_l;
            sample_r = sample_link * mul_r;
            break;
        case rightSample:
            sample_l = sample_link * mul_l;
            sample_r = sample_data * mul_r;
            break;
        // linkedSample enum value has a vague description in the official spec so this will not be implemented
        default:
            sample_l = sample_data * mul_l;
            sample_r = sample_data * mul_r;
            break;
        }

        // Handle filter
        {
            const float n_mod_env_contrib = (100 + std::clamp(mod_env.value, -100.f, 0.f)) * preset_zone.mod_env_to_filter / 120000.f;
            const float n_mod_lfo_contrib = mod_lfo.state * preset_zone.mod_lfo_to_filter / 1200.f;
            filter.cutoff = preset_zone.filter.cutoff * powf(2.0f, n_mod_env_contrib + n_mod_lfo_contrib);
            filter.update(time_per_sample, sample_l, sample_r);
        }

        return { static_cast<sample_t>(sample_l), static_cast<sample_t>(sample_r) };
    }

    float WavetableOscillator::sample_from_index(int index, bool is_linked_sample) const {
        // This is for interpolation, samples outside the range are zero
        if (index < 0) {
            return 0.0f;
        }

        // Handle looping
        const u32 loop_start = sample.loop_start + preset_zone.sample_loop_start_offset;
        const u32 loop_end = sample.loop_end + preset_zone.sample_loop_end_offset;
        if (preset_zone.loop_enable && index > static_cast<int>(loop_end)) {
            index -= static_cast<int>(loop_start);
            index %= static_cast<int>(loop_end - loop_start);
            index += static_cast<int>(loop_start);
        }

        // Fix for crackling on filtered modes
        if (index >= static_cast<int>(sample.length)) {
            return 0.0f;
        }

        // Get sample
        if (is_linked_sample) {
            return static_cast<float>(sample.linked[index]) / 32767.f;
        } else {
            return static_cast<float>(sample.data[index]) / 32767.f;
        }
    }
}
