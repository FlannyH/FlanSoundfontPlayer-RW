#include "WavetableOscillator.h"
#include <algorithm>

namespace Flan {
    BufferSample WavetableOscillator::get_sample(const double time_per_sample, const double pitch_wheel, const int filter_mode) {
        // Immediately skip inactive stage
        if (static_cast<EnvStage>(vol_env.stage) == off) {
            if (midi_key != 255) {
                schedule_kill = true;
            }
            return { static_cast<sample_t>(0.0f), static_cast<sample_t>(0.0f) };
        }

        // Update note parameters
        channel_volume = static_cast<double>(voice_params->FinalLevels.Vol);
        channel_panning = static_cast<double>(voice_params->FinalLevels.Pan);
        channel_pitch = static_cast<double>(voice_params->FinalLevels.Pitch) - initial_channel_pitch;

        // Update envelopes
        vol_env.update(preset_zone.vol_env, time_per_sample, true);
        mod_env.update(preset_zone.mod_env, time_per_sample, false);

        // Update LFOs
        vib_lfo.update(preset_zone.vib_lfo, time_per_sample);
        mod_lfo.update(preset_zone.mod_lfo, time_per_sample);

        // Handle sample progression
        const double pitch_wheel_contrib = (pitch_wheel / 12.0);
        const double channel_pitch_contrib = (channel_pitch / 1200.0);
        const double mod_env_contrib = (((100.0 + mod_env.value) * static_cast<double>(preset_zone.mod_env_to_pitch)) / (1200.0 * 100.0));
        const double mod_lfo_contrib = ((mod_lfo.state * static_cast<double>(preset_zone.mod_lfo_to_pitch)) / (1200.0));
        const double vib_lfo_contrib = ((vib_lfo.state * static_cast<double>(preset_zone.vib_lfo_to_pitch)) / (1200.0));
        sample_position += sample_delta * pow(2.0, pitch_wheel_contrib + channel_pitch_contrib +  mod_env_contrib + mod_lfo_contrib + vib_lfo_contrib);

        // Handle offsets
        const u32 sample_start = preset_zone.sample_start_offset;
        const u32 length = sample.length + (preset_zone.sample_end_offset - preset_zone.sample_start_offset);
        const u32 loop_start = sample.loop_start + preset_zone.sample_loop_start_offset;
        const u32 loop_end = sample.loop_end + preset_zone.sample_loop_end_offset;

        // Loop around sample loop points, to avoid floating point precision errors with high sample position values (yes, this has happened before lmao)
        if (preset_zone.loop_enable && sample_position > static_cast<double>(loop_end)) {
            sample_position -= static_cast<double>(loop_end - loop_start);
        }

        // If looping is not enabled, and sample finished playing, set channel to off
        if (!preset_zone.loop_enable) {
            if (sample_position > static_cast<double>(length)) {
                vol_env.stage = static_cast<double>(off);
                return { static_cast<sample_t>(0.0f), static_cast<sample_t>(0.0f) };
            }
        }

        // After a lot of headaches and comparing with a bunch of different SoundFont tools like Viena, FluidSynth, and
        // Fruity Soundfont Player, these are the dB to linear conversion magic numbers I've found.
        const double corrected_adsr_volume = pow(2.0, (vol_env.value - (mod_lfo.state * static_cast<double>(preset_zone.mod_lfo_to_volume))) / 6.0)
                                    * pow(2.0, static_cast<double>(-preset_zone.init_attenuation) / 15.0);

        // Calculate stereo volume factors, and calculate the center index
        const double mul_base = corrected_adsr_volume * channel_volume;
        const float mul_l = static_cast<float>(mul_base * ((-(channel_panning) + 1.0) / 2.0) * ((-static_cast<double>(preset_zone.pan) + 1.0) / 2.0));
        const float mul_r = static_cast<float>(mul_base * ((+(channel_panning) + 1.0) / 2.0) * ((+static_cast<double>(preset_zone.pan) + 1.0) / 2.0));
        const int index = static_cast<int>(sample_position) + static_cast<int>(sample_start);

        float sample_data = 0;
        float sample_link = 0;
        // Point sampling (1-tap)
        if (filter_mode == 0) {
            sample_data = sample_from_index(index, false);
            if (sample.type != monoSample) sample_link = sample_from_index(index, true);
        }
        // Linear filtering (2-tap)
        else if (filter_mode == 1) {
            const float t = fmod(static_cast<float>(sample_position), 1.0f);
            const float sample_data1 = sample_from_index(index, false);
            const float sample_data2 = sample_from_index(index + 1, false);
            sample_data = lerp(sample_data1, sample_data2, t);
            if (sample.type != monoSample) {
                const float sample_link1 = sample_from_index(index, true);
                const float sample_link2 = sample_from_index(index + 1, true);
                sample_link = lerp(sample_link1, sample_link2, t);
            }
        }
        // Gaussian filter (4-tap)
        else if (filter_mode == 2) {
            for (int i = -1; i < 3; i++) {
                // Get sample index
                const int sample_index = index + i;

                // Get distance from sample_position
                const double distance = abs(sample_position - static_cast<double>(sample_index));

                // Index bell curve
                sample_data += sample_from_index(sample_index, false) * bell_curve[static_cast<int>(distance * 256) % 512];
                if (sample.type != monoSample) sample_link += sample_from_index(sample_index, false) * bell_curve[static_cast<int>(distance * 256) % 512];
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
            const double n_mod_env_contrib = (100 + std::clamp(mod_env.value, -100.0, 0.0)) * static_cast<double>(preset_zone.mod_env_to_filter) / 120000.0;
            const double n_mod_lfo_contrib = mod_lfo.state * static_cast<double>(preset_zone.mod_lfo_to_filter) / 1200.0;
            filter.cutoff = preset_zone.filter.cutoff * static_cast<float>(pow(2.0, n_mod_env_contrib + n_mod_lfo_contrib));
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
