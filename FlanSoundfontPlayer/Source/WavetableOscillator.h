#pragma once
#include "../../SoundfontStudies/SoundfontStudies/structs.h"
#include <FruityPlug/fp_plugclass.h>
using sample_t = float;

namespace Flan {
    struct BufferSample {
        sample_t left, right;
    };

    inline float bell_curve[512]{ 0.0f };

    const float zero = 0.0f;

    struct WavetableOscillator
    {
        // todo: remove pointers, make copies, there is no reason for these to be potential cache-misses-to-be
        // can even be made const, since we'll be using a vector of wave oscs, so we can set the const values on initialization.
        Sample sample{};                // Current sample that's being played
        Zone preset_zone{};             // Current preset that's being used
        EnvState vol_env{};             // Current volume envelope state
        EnvState mod_env{};             // Current modulator envelope state
        LfoState vib_lfo{};             // Current vibrato lfo envelope state
        LfoState mod_lfo{};             // Current modulator lfo envelope state
        LowPassFilter filter{};         // Filter state
        float sample_position = 0.f;    // Current index into the sample data
        float sample_delta = 0.f;       // How much the sample position should increase every (tick? frame? not sure yet)
        float channel_volume = 0.0f;     // Volume data supplied from external source like a DAW
        float channel_panning = 0.0f;    // Panning data supplied from external source like a DAW
        float initial_channel_pitch = 0.0f;      // Pitch data supplied from external source like a DAW
        float channel_pitch = 0.0f;      // Pitch data supplied from external source like a DAW
        u8 midi_key = 255;              // The current midi key that's playing
        PVoiceParams voice_params = nullptr;

        BufferSample get_sample(float time_per_sample, float pitch_wheel, const int filter_mode = true);
        float sample_from_index(int index, bool is_linked_sample);
    };
}