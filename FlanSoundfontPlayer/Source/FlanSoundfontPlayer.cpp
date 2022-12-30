#include "FlanSoundfontPlayer.h"
#include <windows.h>
#include <ios>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include "../Logging.h"

// Plugin info struct that FL Studio wants
TFruityPlugInfo plug_info = {
    CurrentSDKVersion,
    (char*)"FlanSoundfontPlayer",
    (char*)"FlanSF",
        FPF_Generator |
        FPF_MIDIOut |
        FPF_NewVoiceParams |
        FPF_WantNewTick,
    0, // todo: update this to the actual value
    0,
    0, // todo: figure out what this means
};

// This is what FL Studio calls when it wants to create an instance of the plugin
extern "C" TFruityPlug * _stdcall CreatePlugInstance(TFruityPlugHost* host, int tag)
{
    return new FlanSoundfontPlayer(tag, host);
}

HMODULE dll_handle;

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) 
        dll_handle = module;
    return TRUE;
}

void UpdateRender(FlanSoundfontPlayer* plugin) {
    while (plugin->not_destructing)
    {
        if (plugin->window_safe)
        {
            plugin->renderer.begin_frame();
            // todo: load a font first
            // todo: move lut init somewhere else
            Flan::update_entities(plugin->scene, plugin->renderer, *plugin->input, 1.0f / 165.0f);
            //plugin->renderer.draw_text({ {0,0} , {1280, 720} }, L"Hello World!", { 16, 16 }, { 4, 4 }, { 1, 1, 1, 1 }, 0.0f);
            plugin->renderer.end_frame();
            plugin->input->update(plugin->renderer.window());
        }
    }
}

FlanSoundfontPlayer::FlanSoundfontPlayer(int set_tag, TFruityPlugHost* host) : TCPPFruityPlug(set_tag, host, 0) {
    // Set plugin info so FL Studio knows what type of plugin this is
    Info = &plug_info;

    // Initialize renderer
    renderer.init(1280, 720, true, dll_handle);

    // Attach our OpenGL window to the FL plugin, by getting the HWND from our GLFWwindow and passing it to the plugin struct
    EditorHandle = glfwGetWin32Window(renderer.window());

    // Create a render thread that renders the UI elements
    update_render_thread = std::thread(UpdateRender, this);

    // Create our UI elements
    CreateUI();

    // If a soundfont is loaded
    if (!soundfont.presets.empty()) {
        UpdatePresetDropdownMenu();
    }
    // Otherwise, try to load gm.dls, I mean which Windows PC doesn't have this file, I remember having it on my Windows XP machine
    else {
        soundfont.clear();
        soundfont.from_file("C:/Windows/System32/drivers/gm.dls");
        //strcpy_s(sf2_path, "C:/Windows/System32/drivers/gm.dls");
        UpdatePresetDropdownMenu();
    }

    // Init wave oscillators to off
    for (auto& wave_osc : wave_oscs)
    {
        wave_osc.vol_env.stage = static_cast<float>(Flan::envStage::off);
    }
}

FlanSoundfontPlayer::~FlanSoundfontPlayer()
{
    // Tell the rendering thread we're done
    not_destructing = false;
    update_render_thread.join();

    // Close the input manager
    delete input;

    // Delete the window
    glfwDestroyWindow(gl_window);
    glfwTerminate();
}

intptr_t _stdcall FlanSoundfontPlayer::Dispatcher(intptr_t id, intptr_t index, intptr_t value)
{
    intptr_t r = TCPPFruityPlug::Dispatcher(id, index, value);
    if (r != 0)
        return r;

    switch (id)
    {
        // show or hide the plugin editor 
    case FPD_ShowEditor:
        if (value == 0)	// hide (no parent window)
        {
            window_safe = false;
            glfwDestroyWindow(gl_window);
            DestroyWindow(EditorHandle);
            EditorHandle = 0;
        }
        else			// show
        {
            //EditorHandle = CreateWindowEx(0, L"SawVCExampleWindow", L"SawVCExampleWindow", WS_CHILD | WS_VISIBLE, 0, 0, 300, 200, (HWND)Value, NULL, 0, NULL);
            renderer.init(1280, 720, true, dll_handle);
            EditorHandle = glfwGetWin32Window(renderer.window());
            SetParent(EditorHandle, (HWND)value);
            SetWindowLong(EditorHandle, GWL_STYLE, WS_CHILD | WS_VISIBLE);
            SetWindowLongPtr(EditorHandle, GWLP_USERDATA, (LONG_PTR)this);
            ShowWindow(EditorHandle, SW_SHOW);
            delete input;
            input = new Flan::Input(renderer.window());
            window_safe = true;
            intptr_t index = 0;
            std::vector<std::string> menu_entries;
            while (true) {
                PParamMenuEntry pentry = ((PParamMenuEntry)PlugHost->Dispatcher(HostTag, FHD_GetParamMenuEntry, 0, index++));
                if (!pentry) break;
                menu_entries.push_back(std::string(pentry->Name));
            }
            int x = 0;
            PlugHost->Dispatcher(HostTag, FHD_ParamMenu, 0, 6);
        }
        break;

        // set the samplerate
    case FPD_SetSampleRate:
        AudioRenderer.setSmpRate(*(int*)&value);
        PitchMul = (float)(MiddleCMul / AudioRenderer.getSmpRate());
        break;
    }

    return r;
    return 0;
}

TVoiceHandle _stdcall FlanSoundfontPlayer::TriggerVoice(PVoiceParams voice_params, intptr_t set_tag)
{
    Flan::WavetableOscillator* return_value = nullptr;

    // Debug
    swprintf_s(debug_buffer, L"InitLevels:\n\tPan:\t%f\n\tVol:\t%f\n\tPitch:\t%f\n\tFCut:\t%f\n\tFRes:\t%f\nFinalLevels:\n\tPan:\t%f\n\tVol:\t%f\n\tPitch:\t%f\n\tFCut:\t%f\n\tFRes:\t%f\n", 
        voice_params->InitLevels.Pan,
        voice_params->InitLevels.Vol,
        voice_params->InitLevels.Pitch,
        voice_params->InitLevels.FCut,
        voice_params->InitLevels.FRes,      
        voice_params->FinalLevels.Pan,
        voice_params->FinalLevels.Vol,
        voice_params->FinalLevels.Pitch,
        voice_params->FinalLevels.FCut,
        voice_params->FinalLevels.FRes
    );

    scene.value_pool.set_ptr<wchar_t>("text_debug", debug_buffer);

    // Get preset from currently selected index
    const Flan::Preset& preset = soundfont.presets[dropdown_indices_inverse[preset_dropdown->current_selected_index]];

    // Get midi information
    float corrected_key = 60 + (voice_params->FinalLevels.Pitch / 100);
    int key = (int)(60 + (voice_params->FinalLevels.Pitch / 100));
    int vel = (int)(voice_params->InitLevels.Vol * 63.0f);

    // Loop over all preset zones to figure out for which ones the key and the velocity are inside the range
    for (auto& zone : preset.zones) {
        // for the zones that fit that criteria:
        if (static_cast<u8>(corrected_key) >= zone.key_range_low &&
            static_cast<u8>(corrected_key) <= zone.key_range_high &&
            vel >= zone.vel_range_low &&
            vel <= zone.vel_range_high
            ) {

            // find a free wavetable oscillator spot in the array
            auto& wave_osc = wave_oscs[curr_wave_osc_idx];
            return_value = &wave_osc;
            curr_wave_osc_idx = (curr_wave_osc_idx + 1) % N_WAVE_OSCS;
            {
                // init sample and preset pointers
                wave_osc.sample = soundfont.samples[zone.sample_index];
                wave_osc.preset_zone = zone;

                // apply overrides
                if (scene.value_pool.get<double>("delay") != 0.0) {
                    wave_osc.preset_zone.vol_env.delay = 1.0f / static_cast<float>(scene.value_pool.get<double>("delay"));
                }
                if (scene.value_pool.get<double>("attack") != 0.0) {
                    wave_osc.preset_zone.vol_env.attack = 1.0f / static_cast<float>(scene.value_pool.get<double>("attack"));
                }
                if (scene.value_pool.get<double>("hold") != 0.0) {
                    wave_osc.preset_zone.vol_env.hold = 1.0f / static_cast<float>(scene.value_pool.get<double>("hold"));
                }
                if (scene.value_pool.get<double>("decay") != 0.0) {
                    wave_osc.preset_zone.vol_env.decay = 100.0f / static_cast<float>(scene.value_pool.get<double>("decay"));
                }
                if (scene.value_pool.get<double>("sustain") != 0.0) {
                    wave_osc.preset_zone.vol_env.sustain = static_cast<float>(scene.value_pool.get<double>("sustain"));
                }
                if (scene.value_pool.get<double>("release") != 0.0) {
                    wave_osc.preset_zone.vol_env.release = 100.0f / static_cast<float>(scene.value_pool.get<double>("release"));
                }

                // init sample position and adsr_volume to 0.0
                wave_osc.sample_position = 0.0f;
                wave_osc.vol_env.value = 0.0f;
                wave_osc.mod_env.value = 0.0f;

                // init adsr_stage to Delay
                wave_osc.vol_env.stage = static_cast<float>(Flan::envStage::delay);
                wave_osc.mod_env.stage = static_cast<float>(Flan::envStage::delay);

                // init lfo
                wave_osc.vib_lfo.time = 0.0f;
                wave_osc.vib_lfo.state = 0.0f;
                wave_osc.mod_lfo.time = 0.0f;
                wave_osc.mod_lfo.state = 0.0f;

                // init filter
                wave_osc.filter = zone.filter;

                // set midi key, velocity to note_on event key, velocity
                wave_osc.midi_key = static_cast<u8>(key);
                if (zone.vel_override < 128)
                    vel = zone.vel_override;
                wave_osc.initial_channel_pitch = voice_params->FinalLevels.Pitch;
                wave_osc.voice_params = voice_params;
                wave_osc.channel_pitch = 0.0f;

                // init sample_delta
                const float pitch_correction = (wave_osc.channel_pitch / 100.0f) + static_cast<float>(zone.root_key_offset) + static_cast<float>(zone.tuning);
                if (zone.key_override < 128)
                    key = zone.key_override;
                const float scaled_key = ((static_cast<float>(key - 60) * zone.scale_tuning));
                const float key_multiplier = powf(2.0f, (scaled_key) / 12.f); /*Flan::lerp(
                    curr_scale[static_cast<size_t>(scaled_key)],
                    curr_scale[static_cast<size_t>(scaled_key) + 1],
                    fmodf(scaled_key, 1.0f));*/ // todo: make this work with scales
                wave_osc.sample_delta = (wave_osc.sample.base_sample_rate * key_multiplier * (powf(2.0f, pitch_correction / 12.0f))) / 44100.f;
                wave_osc.preset_zone.vol_env.hold *= powf(2.f, wave_osc.preset_zone.key_to_vol_env_hold * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.vol_env.decay *= powf(2.f, wave_osc.preset_zone.key_to_vol_env_decay * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.mod_env.hold *= powf(2.f, wave_osc.preset_zone.key_to_mod_env_hold * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.mod_env.decay *= powf(2.f, wave_osc.preset_zone.key_to_mod_env_decay * static_cast<float>(key - 60) / (1200));
            }
        }
    }
    // Start note
    return (TVoiceHandle)return_value;
}

void _stdcall FlanSoundfontPlayer::Voice_Release(TVoiceHandle handle)
{
    Flan::WavetableOscillator* wave_osc = (Flan::WavetableOscillator*)handle;
    wave_osc->vol_env.stage = Flan::envStage::release;
}

// MIDI values here used for pitch wheel
int _stdcall FlanSoundfontPlayer::ProcessEvent(int event_id, int event_value, int flags)
{
    switch (event_id) {
    case FPE_Tempo:
        swprintf_s(debug_buffer, L"Tempo changed to %f", *(float*)&event_value);
        break;
    case FPE_MaxPoly:
        swprintf_s(debug_buffer, L"Max polyphony changed to %i", event_value);
        break;
    case FPE_MIDI_Pan:
        swprintf_s(debug_buffer, L"MIDI Pan changed to %i", event_value);
        break;
    case FPE_MIDI_Vol:
        swprintf_s(debug_buffer, L"MIDI Vol changed to %i", event_value);
        break;
    case FPE_MIDI_Pitch:
        swprintf_s(debug_buffer, L"MIDI Pitch changed to %i", event_value);
        break;
    default:
        return 0;
        break;
    }
    scene.value_pool.set_ptr<wchar_t>("text_debug", debug_buffer);
    return 0;
}

void _stdcall FlanSoundfontPlayer::Gen_Render(PWAV32FS dest_buffer, int& length)
{
    // Fill buffer
    float* dest = (float*)dest_buffer;
    for (int j = 0; j < length; j++) {
        sample_t total_l = 0;
        sample_t total_r = 0;
        for (auto& wave_osc : wave_oscs) {
            // todo: un-hardcode the sample rate
            // todo: implement pitch wheel and note slides
            const Flan::BufferSample sample = wave_osc.get_sample(1.0f / 44100.0f, 0.0f, static_cast<int>(scene.value_pool.get<double>("sampling_mode")));
            total_l += sample.left;
            total_r += sample.right;
        }
        dest[(j * 2) + 0] = total_l;
        dest[(j * 2) + 1] = total_r;
    }

    // Kill dead voices
    for (auto& wave_osc : wave_oscs) {
        if (wave_osc.schedule_kill) {
            PlugHost->Voice_Kill((intptr_t)&wave_osc, false);
            wave_osc.midi_key = 255;
            wave_osc.schedule_kill = false;
        }
    }
}

void _stdcall FlanSoundfontPlayer::Idle()
{
}

void FlanSoundfontPlayer::CreateUI()
{    
    //--------------------------
    // Set up components
    // Create bank text
    {
        Flan::Transform text_bank_transform{
            {20, 20},
            {180, 60},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_bank", text_bank_transform, {
            L"Bank:",
            {2, 2},
            {1, 1, 1, 1},
            Flan::AnchorPoint::center,
            Flan::AnchorPoint::center
            });
    }
    // Create bank numberbox
    {
        Flan::Transform nb_bank_transform{
            {20, 60},
            {180, 140},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::NumberRange nb_bank_number_range{ 0, 127, 1, 0, 0 };
        auto entity = Flan::create_numberbox(scene, "bank", nb_bank_transform, nb_bank_number_range);
        Flan::add_function(scene, entity, [&]() {
            // Get current values of bank and program
            u16 bank = (u16)scene.value_pool.get<double>("bank");
            u16 program = (u16)scene.value_pool.get<double>("program");
            u16 preset_key = (bank << 8) | program;

            // If the soundfont does not contain a preset at this key, the selection is invalid
            if (!soundfont.presets.contains(preset_key)) {
                preset_dropdown->current_selected_index = -1;
                return;
            }

            // Otherwise, set the current index of the dropdown to match the preset
            preset_dropdown->current_selected_index = dropdown_indices[preset_key];
        });
    }
    // Create program text
    {
        Flan::Transform text_program_transform{
            {180, 20},
            {340, 60},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_program", text_program_transform, {
            L"Program:",
            {2, 2},
            {1, 1, 1, 1},
            Flan::AnchorPoint::center,
            Flan::AnchorPoint::center
            });
    }
    // Create program numberbox
    {
        Flan::Transform nb_program_transform{
            {180, 60},
            {340, 140},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::NumberRange nb_program_number_range{ 0, 127, 1, 0, 0 };
        Flan::EntityID entity = Flan::create_numberbox(scene, "program", nb_program_transform, nb_program_number_range);

        Flan::add_function(scene, entity, [&]() {
            // Get current values of bank and program
            u16 bank = (u16)scene.value_pool.get<double>("bank");
            u16 program = (u16)scene.value_pool.get<double>("program");
            u16 preset_key = (bank << 8) | program;

            // If the soundfont does not contain a preset at this key, the selection is invalid
            if (!soundfont.presets.contains(preset_key)) {
                preset_dropdown->current_selected_index = -1;
                return;
            }

            // Otherwise, set the current index of the dropdown to match the preset
            preset_dropdown->current_selected_index = dropdown_indices[preset_key];
        });
    }
    // Create preset text
    {
        Flan::Transform text_preset_transform{
            {340, 20},
            {720, 60},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_preset", text_preset_transform, {
            L"Selected preset:",
            {2, 2},
            {1, 1, 1, 1},
            Flan::AnchorPoint::center,
            Flan::AnchorPoint::center
            });
    }
    // Create dropdown menu for soundfont presets
    {
        Flan::Transform db_program_transform{
            {340, 60},
            {720, 140},
            0.1f,
            Flan::AnchorPoint::top_left
        };
        auto combobox_entity = Flan::create_combobox(scene, "combobox_preset", db_program_transform, { L"000:000 - Piano 1", L"000:001 - Piano 2" });
        preset_dropdown = scene.get_component<Flan::Combobox>(combobox_entity);
        Flan::add_function(scene, combobox_entity, [&]() {
            auto index = preset_dropdown->current_selected_index;
            double bank = (double)(dropdown_indices_inverse[index] >> 8);
            double program = (double)(dropdown_indices_inverse[index] & 0xFF);
            scene.value_pool.set_value<double>("program", program);
            scene.value_pool.set_value<double>("bank", bank);
        });
    }
    // Create textbox for file browser
    {
        Flan::Transform text_soundfont_transform{
            {20, 160},
            {640, 240},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_soundfont_path", text_soundfont_transform, {
            L"C:/Windows/System32/drivers/gm.dls",
            {2, 2},
            {1, 1, 1, 1},
            Flan::AnchorPoint::left,
            Flan::AnchorPoint::left,
            }, true);
    }
    // Create button for file browser
    {
        Flan::Transform button_soundfont_transform{
            {640, 160},
            {720, 240},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_button(scene, button_soundfont_transform, [&]()
            {
                printf("hi!\n");
            }, { L"...", {2, 2}, {0, 0, 0, 1}, Flan::AnchorPoint::center, Flan::AnchorPoint::center });
    }
    // Create sliders for ADSR
    {
        // Define parameters for loop
        float stride = 117.0f;
        std::string names[6] = {
            "delay",
            "attack",
            "hold",
            "decay",
            "sustain",
            "release",
        };
        std::wstring text[6] = {
            L"Delay",
            L"Attack",
            L"Hold",
            L"Decay",
            L"Sustain",
            L"Release",
        };
        Flan::NumberRange ranges[6] = {
            {0, 2, 0.01, 0.0, 2}, // delay (seconds)
            {0, 4, 0.02, 0.0, 2}, // attack (seconds)
            {0, 4, 0.02, 0.0, 2}, // hold (seconds)
            {0, 10, 0.05, 0.0, 2}, // decay (seconds)
            {-40, 40, 0.2, 0.0, 1}, // sustain (dB)
            {0, 10, 0.05, 0.0, 2}, // release (seconds)
        };
        for (size_t i = 0; i < 6; ++i) {
            Flan::Transform text_transform{
                {20 + stride * i, 320},
                {20 + stride * (i + 1), 360},
            };
            Flan::Transform slider_transform{
                {20 + stride * i, 360},
                {20 + stride * (i + 1), 640},
            };
            Flan::create_slider(scene, names[i], slider_transform, ranges[i], true);
            Flan::create_text(scene, "text_" + names[i], text_transform, { text[i], {2, 2}, {1, 1, 1, 1}, Flan::AnchorPoint::center, Flan::AnchorPoint::center }, false);
        }

        Flan::create_text(scene, "text_overrides", {
            {20, 280},
            {640, 320},
            }, { L"Relative volume envelope overrides:", {2, 2}, {1, 1, 1, 1}, Flan::AnchorPoint::left, Flan::AnchorPoint::left }, false);
    }
    // Create textbox for scale browser
    {
        Flan::Transform text_scale_transform{
            {760, 60},
            {1180, 140},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_scale_path", text_scale_transform, {
            L"12-TET",
            {2, 2},
            {1, 1, 1, 1},
            Flan::AnchorPoint::left,
            Flan::AnchorPoint::left,
            }, true);
    }
    // Create button for scale browser
    {
        Flan::Transform button_scale_transform{
            {1180, 60},
            {1260, 140},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_button(scene, button_scale_transform, [&]()
            {
                printf("hi2!\n");
            }, { L"...", {2, 2}, {0, 0, 0, 1}, Flan::AnchorPoint::center, Flan::AnchorPoint::center });
    }
    // Create radio button for sampling mode
    {
        Flan::Transform text_sampling_transform{
            {760, 370},
            {1260, 450},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::Transform radio_button_sampling_transform{
            {760, 450},
            {1260, 650},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_sampling_mode", text_sampling_transform, {
            L"Sampling mode",
            {3, 3},
            {1, 1, 1, 1},
            Flan::AnchorPoint::left,
            Flan::AnchorPoint::left,
            }, false);
        Flan::create_radio_button(scene, "sampling_mode", radio_button_sampling_transform, {
            L"Point sampling (1-point)",
            L"Linear sampling (2-point)",
            L"Gaussian sampling (4-point)",
            }, 0);
    }
    // Debug text
    {
        Flan::Transform text_debug_transform{
            {760, 120},
            {1260, 370},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::create_text(scene, "text_debug", text_debug_transform, {
            L"debug text!",
            {1, 1},
            {1, 1, 1, 1},
            Flan::AnchorPoint::left,
            Flan::AnchorPoint::left,
            }, false);
    }
}

void FlanSoundfontPlayer::UpdatePresetDropdownMenu()
{
    // Clear the list of presets
    preset_dropdown->list_items.clear();
    dropdown_indices.clear();
    dropdown_indices_inverse.clear();

    // Loop over all the soundfont presets
    for (auto& preset : soundfont.presets) {
        // Get the bank and program for the current one
        auto bank = (preset.first & 0xFF00) >> 8;
        auto program = (preset.first & 0x00FF);

        // Convert name to wstring
        std::wstring name;
        name.resize(preset.second.name.length() + 10);

        // Add bank number and program number to the preset name
        name[0] = L'0' + (bank / 100) % 10;
        name[1] = L'0' + (bank / 10) % 10;
        name[2] = L'0' + (bank / 1) % 10;
        name[3] = ':';
        name[4] = L'0' + (program / 100) % 10;
        name[5] = L'0' + (program / 10) % 10;
        name[6] = L'0' + (program / 1) % 10;
        name[7] = ' ';
        name[8] = '-';
        name[9] = ' ';

        // Add the actual name from the soundfont data
        for (size_t i = 0; i < preset.second.name.length(); i++) {
            name[i + 10] = preset.second.name[i];
        }

        // Add the preset to the list
        preset_dropdown->list_items.push_back(name);

        // Add the index to the indices map, so we can update dropdown menu when the bank/program numberboxes update
        dropdown_indices[preset.first] = (int)dropdown_indices_inverse.size();

        // Add the bank and program to the inverse list, so we can update the bank and program number boxes when the dropdown menu updates
        dropdown_indices_inverse.push_back(preset.first);

    }
}
