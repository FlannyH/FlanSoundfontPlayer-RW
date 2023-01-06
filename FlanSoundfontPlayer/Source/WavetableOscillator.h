#pragma once
#include "../../SoundfontStudies/SoundfontStudies/structs.h"
#include <FruityPlug/fp_plugclass.h>
using sample_t = float;

namespace Flan {
    struct BufferSample {
        sample_t left, right;
    };

    inline float bell_curve[512]{ 0.0f };

    struct WavetableOscillator
    {
        // can even be made const, since we'll be using a vector of wave oscs, so we can set the const values on initialization.
        Sample sample{};                // Current sample that's being played
        Zone preset_zone{};             // Current preset that's being used
        EnvState vol_env{};             // Current volume envelope state
        EnvState mod_env{};             // Current modulator envelope state
        LfoState vib_lfo{};             // Current vibrato lfo envelope state
        LfoState mod_lfo{};             // Current modulator lfo envelope state
        LowPassFilter filter{};         // Filter state
        double sample_position = 0.0;    // Current index into the sample data
        double sample_delta = 0.0;       // How much the sample position should increase every (tick? frame? not sure yet)
        double channel_volume = 0.0;     // Volume data supplied from external source like a DAW
        double channel_panning = 0.0;    // Panning data supplied from external source like a DAW
        double initial_channel_pitch = 0.0;      // Pitch data supplied from external source like a DAW
        double channel_pitch = 0.0;      // Pitch data supplied from external source like a DAW
        u8 midi_key = 255;              // The current midi key that's playing
        bool schedule_kill = false;
        PVoiceParams voice_params = nullptr;

        BufferSample get_sample(double time_per_sample, double pitch_wheel, int filter_mode = true);
        [[nodiscard]] float sample_from_index(int index, bool is_linked_sample) const;
    };

    struct Voice {
        std::vector<WavetableOscillator*> wave_oscs;
        intptr_t voice_tag = 0;
        bool schedule_kill = false;

        [[nodiscard]] BufferSample get_sample(double time_per_sample, double pitch_wheel, int filter_mode = true);

        void release() const {
            for (const auto osc : wave_oscs) {
                osc->vol_env.stage = Flan::EnvStage::release;
            }
        }
    };
}