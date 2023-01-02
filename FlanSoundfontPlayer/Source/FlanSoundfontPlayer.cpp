#include "FlanSoundfontPlayer.h"
#include <windows.h>
#include <ios>
#include <cstdio>

// Plugin info struct that FL Studio wants
TFruityPlugInfo plug_info = {
    CurrentSDKVersion,
    const_cast<char*>("FlanSoundfontPlayer"),
    const_cast<char*>("FlanSF"),
        FPF_Generator |
        FPF_MIDIOut |
        FPF_NewVoiceParams |
        FPF_WantNewTick,
    0, // todo: update this to the actual value
    0,
    0, // todo: figure out what this means
};

// This is what FL Studio calls when it wants to create an instance of the plugin
// ReSharper disable once CppInconsistentNaming
// ReSharper disable once CppParameterMayBeConst
extern "C" TFruityPlug * _stdcall CreatePlugInstance(TFruityPlugHost* host, int tag)
{
    return new FlanSoundfontPlayer(tag, host);
}

HMODULE dll_handle;

// ReSharper disable once CppInconsistentNaming
// ReSharper disable twice CppParameterMayBeConst
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    // This if statement only runs code when the plugin is loaded for the first time. This is perfect to initialized shared resources like look-up tables
    if (reason == DLL_PROCESS_ATTACH) {
        // Store the dll handle, to be able to load resources from the dll file
        dll_handle = module;

        // Generate gauss table, credit to https://problemkaputt.de/fullsnes.htm#snesaudioprocessingunitapu for providing the gauss table that is approximated below
        // Formula was made through trial and error in geogebra
        for (int ix = 0; ix < 512; ix++) {
            const float x_270 = static_cast<float>(ix) / 270.f;
            const float x_512 = static_cast<float>(ix) / 512.f;
            const float result = powf(2.718281828f, -x_270 * x_270) * 1305.f * powf((1 - (x_512 * x_512)), 1.4f);
            Flan::bell_curve[ix] = result / 2039.f; // magic number to make the volume similar to the other filtering modes
        }
    }
    return TRUE;
}

void update_render(FlanSoundfontPlayer* plugin) {
    while (plugin->not_destructing)
    {
        if (plugin->window_safe)
        {
            std::lock_guard guard(plugin->graphics_thread_lock);
            plugin->renderer.begin_frame();
            // todo: load a font first
            // todo: move lut init somewhere else
            Flan::update_entities(plugin->scene, plugin->renderer, *plugin->input, 1.0f / 165.0f);
            //plugin->renderer.draw_text({ {0,0} , {1280, 720} }, L"Hello World!", { 16, 16 }, { 4, 4 }, { 1, 1, 1, 1 }, 0.0f);
            plugin->renderer.end_frame();
            plugin->input->update(plugin->renderer.window());
        }
        if (!plugin->soundfont_to_load.empty()) {
            plugin->load_soundfont(plugin->soundfont_to_load);
            plugin->soundfont_to_load.clear();
        }
    }
}

FlanSoundfontPlayer::FlanSoundfontPlayer(int set_tag, TFruityPlugHost* host) : TCPPFruityPlug(set_tag, host, nullptr) {
    // Set plugin info so FL Studio knows what type of plugin this is
    Info = &plug_info;

    // Initialize renderer
    renderer.init(1280, 720, true, dll_handle);
    input = new Flan::Input(renderer.window());

    // Attach our OpenGL window to the FL plugin, by getting the HWND from our GLFWwindow and passing it to the plugin struct
    EditorHandle = glfwGetWin32Window(renderer.window());

    host->Dispatcher(set_tag, FHD_WantIdle, 0, 1);

    // Create our UI elements
    create_ui();

    // Create render thread
    m_update_render_thread = std::thread(update_render, this);

    // If a soundfont is loaded
    if (!m_soundfont.presets.empty()) {
        update_preset_dropdown_menu();
    }
    // Otherwise, try to load gm.dls, I mean which Windows PC doesn't have this file, I remember having it on my Windows XP machine
    else {
        m_soundfont.clear();
        m_soundfont.from_file("C:/Windows/System32/drivers/gm.dls");
        //strcpy_s(sf2_path, "C:/Windows/System32/drivers/gm.dls");
        update_preset_dropdown_menu();
    }

    // Init wave oscillators to off
    for (const auto& voice : m_active_voices) {
        for (const auto& wave_osc : voice->wave_oscs) {
            wave_osc->vol_env.stage = static_cast<float>(Flan::envStage::off);
        }
    }
}

FlanSoundfontPlayer::~FlanSoundfontPlayer()
{
    // Tell the rendering thread we're done
    not_destructing = false;

    // Wait for the rendering thread to finish
    m_update_render_thread.join();

    // Close the input manager
    delete input;

    // Delete the window
    glfwDestroyWindow(renderer.window());
    glfwTerminate();
}

intptr_t _stdcall FlanSoundfontPlayer::Dispatcher(intptr_t id, intptr_t index, intptr_t value)
{
    const intptr_t r = TCPPFruityPlug::Dispatcher(id, index, value);
    if (r != 0)
        return r;

    switch (id)
    {
        // show or hide the plugin editor 
    case FPD_ShowEditor:
        if (value == 0)	// hide (no parent window)
        {
            // Make sure the renderer stops rendering when the window is hdden
            window_safe = false;

            // Hide the window
            glfwHideWindow(renderer.window());

            // Remove the editor handle the parent
            SetParent(EditorHandle, nullptr);
        }
        else // show
        {
            // Parent the editor handle to the window provided by FL
            SetParent(EditorHandle, reinterpret_cast<HWND>(value));

            // Show the window
            glfwShowWindow(renderer.window());

            // Let the rendering thread know we're good to go
            window_safe = true;
        }

        // Set the editor plugin handle to be this plugin
        SetWindowLongPtr(EditorHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        break;

        // set the samplerate
    case FPD_SetSampleRate:
        AudioRenderer.setSmpRate(static_cast<int>(value));
        PitchMul = static_cast<float>(MiddleCMul / AudioRenderer.getSmpRate());
        m_sample_rate = static_cast<float>(value);
        m_sample_rate_inv = 1.0f / m_sample_rate;
        break;
    default:
        printf("a");
        break;
    }

    return r;
}

TVoiceHandle _stdcall FlanSoundfontPlayer::TriggerVoice(PVoiceParams voice_params, intptr_t set_tag)
{
    // Don't create a new voice if the currently selected index is -1 (invalid)
    if (m_preset_dropdown->current_selected_index == -1) {
        return 0;
    }

    // Create new voice
    Flan::Voice* new_voice = new Flan::Voice();
    new_voice->voice_tag = set_tag;

    // Add it to the active voices
    m_active_voices.push_back(new_voice);

    // Debug
    swprintf_s(m_debug_buffer, L"InitLevels:\n\tPan:\t%f\n\tVol:\t%f\n\tPitch:\t%f\n\tFCut:\t%f\n\tFRes:\t%f\nFinalLevels:\n\tPan:\t%f\n\tVol:\t%f\n\tPitch:\t%f\n\tFCut:\t%f\n\tFRes:\t%f\n", 
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

    if (set_tag == 0) {
        swprintf_s(m_debug_buffer, L"it was zero for some reason");
    }
    scene.value_pool.set_ptr<wchar_t>("text_debug", m_debug_buffer);

    // Get preset from currently selected index
    const Flan::Preset& preset = m_soundfont.presets[m_dropdown_indices_inverse[m_preset_dropdown->current_selected_index]];

    // Get midi information
    const float corrected_key = 60 + (voice_params->FinalLevels.Pitch / 100);
    int key = static_cast<int>(60 + (voice_params->FinalLevels.Pitch / 100));
    int vel = static_cast<int>(voice_params->InitLevels.Vol * 63.0f);

    // Loop over all preset zones to figure out for which ones the key and the velocity are inside the range
    for (auto& zone : preset.zones) {
        // for the zones that fit that criteria:
        if (static_cast<u8>(corrected_key) >= zone.key_range_low &&
            static_cast<u8>(corrected_key) <= zone.key_range_high &&
            vel >= zone.vel_range_low &&
            vel <= zone.vel_range_high
            ) {

            // find a free wavetable oscillator spot in the array
            auto p_wave_osc = new Flan::WavetableOscillator();
            auto& wave_osc = *p_wave_osc;
            {
                // Lock the wavetables so we don't get any surprises from another thread
                std::lock_guard guard{ m_note_playing_mutex };
                new_voice->wave_oscs.push_back(p_wave_osc);
            }

            //m_curr_wave_osc_idx = (m_curr_wave_osc_idx + 1) % N_WAVE_OSCS;
            {
                // init sample and preset pointers
                wave_osc.sample = m_soundfont.samples[zone.sample_index];
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
                wave_osc.sample_delta = (wave_osc.sample.base_sample_rate * key_multiplier * (powf(2.0f, pitch_correction / 12.0f))) * m_sample_rate_inv;
                wave_osc.preset_zone.vol_env.hold *= powf(2.f, wave_osc.preset_zone.key_to_vol_env_hold * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.vol_env.decay *= powf(2.f, wave_osc.preset_zone.key_to_vol_env_decay * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.mod_env.hold *= powf(2.f, wave_osc.preset_zone.key_to_mod_env_hold * static_cast<float>(key - 60) / (1200));
                wave_osc.preset_zone.mod_env.decay *= powf(2.f, wave_osc.preset_zone.key_to_mod_env_decay * static_cast<float>(key - 60) / (1200));
            }
        }
    }
    // Start note
    return reinterpret_cast<TVoiceHandle>(new_voice);
}

void _stdcall FlanSoundfontPlayer::Voice_Release(TVoiceHandle handle)
{
    if (!handle) return;
    Flan::Voice* voice = reinterpret_cast<Flan::Voice*>(handle);
    voice->release();
}

// MIDI values here used for pitch wheel
int _stdcall FlanSoundfontPlayer::ProcessEvent(int event_id, int event_value, int flags)
{
    switch (event_id) {
    case FPE_Tempo:
        swprintf_s(m_debug_buffer, L"Tempo changed to %f", *reinterpret_cast<float*>(&event_value));
        break;
    case FPE_MaxPoly:
        swprintf_s(m_debug_buffer, L"Max polyphony changed to %i", event_value);
        break;
    case FPE_MIDI_Pan:
        swprintf_s(m_debug_buffer, L"MIDI Pan changed to %i", event_value);
        break;
    case FPE_MIDI_Vol:
        swprintf_s(m_debug_buffer, L"MIDI Vol changed to %i", event_value);
        break;
    case FPE_MIDI_Pitch:
        swprintf_s(m_debug_buffer, L"MIDI Pitch changed to %i", event_value);
        m_midi_pitch = static_cast<float>(event_value) / 100.f;
        break;
    default:
        return 0;
    }
    scene.value_pool.set_ptr<wchar_t>("text_debug", m_debug_buffer);
    return 0;
}

void _stdcall FlanSoundfontPlayer::Gen_Render(PWAV32FS dest_buffer, int& length)
{
    // Lock the wavetables so we don't get any surprises from another thread
    std::lock_guard guard{ m_note_playing_mutex };

    // Fill buffer
    float* dest = reinterpret_cast<float*>(dest_buffer);
    for (int j = 0; j < length; j++) {
        sample_t total_l = 0;
        sample_t total_r = 0;
        for (auto* voice : m_active_voices) {
            const Flan::BufferSample sample = voice->get_sample(m_sample_rate_inv, m_midi_pitch, static_cast<int>(scene.value_pool.get<double>("sampling_mode")));
            total_l += sample.left;
            total_r += sample.right;
        }
        dest[(j * 2) + 0] = total_l;
        dest[(j * 2) + 1] = total_r;
    }

    // Kill dead voices - only one per render though
    for (size_t i = 0; i < m_active_voices.size(); i++) {
        if (m_active_voices[i]->schedule_kill) {
            PlugHost->Voice_Kill(m_active_voices[i]->voice_tag, true);
            m_active_voices.erase(m_active_voices.begin() + i);
            break;
        }
    }
}

void FlanSoundfontPlayer::SaveRestoreState(IStream* stream, BOOL save) {
    // Use `stream` to push or pull values.
    // If `save` is true, we push, otherwise we pull
    struct {
        wchar_t soundfont_path[260];
        u16 bank_program;
        double volenv_delay;
        double volenv_attack;
        double volenv_hold;
        double volenv_decay;
        double volenv_sustain;
        double volenv_release;
        u8 sampling_mode;
        // todo: add scale to this struct
    } state{};

    // Handle saving
    if (save) {
        // Populate struct with values
        // Copy soundfont path
        wcscpy_s(state.soundfont_path, _countof(state.soundfont_path), scene.value_pool.get<wchar_t*>("text_soundfont_path"));

        // Copy currently selected bank/program
        state.bank_program  = static_cast<uint16_t>(scene.value_pool.get<double>("bank")) << 8;
        state.bank_program |= static_cast<uint16_t>(scene.value_pool.get<double>("program")) & 0xFF;

        // Copy volume envelope override settings
        state.volenv_delay   = scene.value_pool.get<double>("delay");
        state.volenv_attack  = scene.value_pool.get<double>("attack");
        state.volenv_hold    = scene.value_pool.get<double>("hold");
        state.volenv_decay   = scene.value_pool.get<double>("decay");
        state.volenv_sustain = scene.value_pool.get<double>("sustain");
        state.volenv_release = scene.value_pool.get<double>("release");

        // Copy sampling mode
        state.sampling_mode = static_cast<uint8_t>(scene.value_pool.get<double>("sampling_mode"));

        // todo: add scale to this

        // Write data
        ULONG n_bytes_saved;
        stream->Write(&state, sizeof(state), &n_bytes_saved);
    }

    // Handle loading
    else {
        // Read data
        ULONG n_bytes_loaded;
        stream->Read(&state, sizeof(state), &n_bytes_loaded);

        // Extract data from struct
        // Copy soundfont path and delete whatever was there before
        free(scene.value_pool.get<wchar_t*>("text_soundfont_path"));
        wchar_t* soundfont_path = new wchar_t[260];
        wcscpy_s(soundfont_path, _countof(state.soundfont_path), state.soundfont_path);
        scene.value_pool.set_ptr("text_soundfont_path", soundfont_path);

        // Load the soundfont
        std::string soundfont_path_8;
        const size_t length = wcslen(soundfont_path);
        soundfont_path_8.resize(length);
        for (size_t i = 0; i < length; ++i) {
            soundfont_path_8[i] = static_cast<char>(soundfont_path[i]);
        }
        load_soundfont(soundfont_path_8);

        // Copy currently selected bank/program
        scene.value_pool.set_value<double>("bank", state.bank_program >> 8);
        scene.value_pool.set_value<double>("program", state.bank_program & 0xFF);

        // Copy volume envelope override settings
        scene.value_pool.set_value<double>("delay",     state.volenv_delay);
        scene.value_pool.set_value<double>("attack",    state.volenv_attack);
        scene.value_pool.set_value<double>("hold",      state.volenv_hold);
        scene.value_pool.set_value<double>("decay",     state.volenv_decay);
        scene.value_pool.set_value<double>("sustain",   state.volenv_sustain);
        scene.value_pool.set_value<double>("release",   state.volenv_release);

        // Copy sampling mode
        scene.value_pool.set_value<double>("sampling_mode", state.sampling_mode);

        // todo: add scale to this
    }
}

void _stdcall FlanSoundfontPlayer::Idle()
{
}

void FlanSoundfontPlayer::create_ui()
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
        Flan::NumberRange nb_bank_number_range{ 0, 255, 1, 0, 0 };
        auto entity = Flan::create_numberbox(scene, "bank", nb_bank_transform, nb_bank_number_range);
        Flan::add_function(scene, entity, [&]() {
            // Get current values of bank and program
            const u16 bank = static_cast<uint16_t>(scene.value_pool.get<double>("bank"));
            const u16 program = static_cast<uint16_t>(scene.value_pool.get<double>("program"));
            const u16 preset_key = (bank << 8) | program;

            // If the soundfont does not contain a preset at this key, the selection is invalid
            if (!m_soundfont.presets.contains(preset_key)) {
                m_preset_dropdown->current_selected_index = -1;
                return;
            }

            // Otherwise, set the current index of the dropdown to match the preset
            m_preset_dropdown->current_selected_index = m_dropdown_indices[preset_key];
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
        Flan::NumberRange nb_program_number_range{ 0, 255, 1, 0, 0 };
        Flan::EntityID entity = Flan::create_numberbox(scene, "program", nb_program_transform, nb_program_number_range);

        Flan::add_function(scene, entity, [&]() {
            // Get current values of bank and program
            const u16 bank = static_cast<uint16_t>(scene.value_pool.get<double>("bank"));
            const u16 program = static_cast<uint16_t>(scene.value_pool.get<double>("program"));
            const u16 preset_key = (bank << 8) | program;

            // If the soundfont does not contain a preset at this key, the selection is invalid
            if (!m_soundfont.presets.contains(preset_key)) {
                m_preset_dropdown->current_selected_index = -1;
                return;
            }

            // Otherwise, set the current index of the dropdown to match the preset
            m_preset_dropdown->current_selected_index = m_dropdown_indices[preset_key];
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
        m_preset_dropdown = scene.get_component<Flan::Combobox>(combobox_entity);
        Flan::add_function(scene, combobox_entity, [&]() {
            const auto index = m_preset_dropdown->current_selected_index;
            const double bank = m_dropdown_indices_inverse[index] >> 8;
            const double program = m_dropdown_indices_inverse[index] & 0xFF;
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
            Flan::AnchorPoint::right,
            Flan::AnchorPoint::right,
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
                // Describe file open dialog
                OPENFILENAME ofn;
                wchar_t sz_file[260];
                ZeroMemory(&ofn, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = nullptr;
                ofn.lpstrFile = sz_file;
                ofn.lpstrFile[0] = '\0';
                ofn.nMaxFile = sizeof(sz_file);
                ofn.lpstrFilter = L"Soundfont\0*.sf2;*.dls\0";
                ofn.nFilterIndex = 1;
                ofn.lpstrFileTitle = nullptr;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = nullptr;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                // Open the dialog
                if (GetOpenFileName(&ofn) == TRUE) {
                    // Stop all audio
                    for (const auto* voice : m_active_voices) {
                        for (auto* wave_osc : voice->wave_oscs) {
                            wave_osc->vol_env.stage = Flan::envStage::off;
                        }
                    }

                    // Convert path to a string
                    std::string path;
                    path.resize(wcslen(sz_file));
                    for (size_t i = 0; i < path.size(); ++i) {
                        path[i] = static_cast<char>(sz_file[i]);
                    }

                    // Load the soundfont
                    soundfont_to_load = path;
                }
            }, { L"...", {2, 2}, {0, 0, 0, 1}, Flan::AnchorPoint::center, Flan::AnchorPoint::center });
    }
    // Create sliders for ADSR
    {
        // Define parameters for loop
        float stride = 117.0f;
        float extra_text_space = 40.f;
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
                {20 - extra_text_space + stride * static_cast<float>(i), 320 - extra_text_space},
                {20 + extra_text_space + stride * static_cast<float>(i + 1), 360 + extra_text_space},
            };
            Flan::Transform slider_transform{
                {20 + stride * static_cast<float>(i), 360},
                {20 + stride * static_cast<float>(i + 1), 640},
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

void FlanSoundfontPlayer::update_preset_dropdown_menu()
{
    // Clear the list of presets
    m_preset_dropdown->list_items.clear();
    m_dropdown_indices.clear();
    m_dropdown_indices_inverse.clear();

    // Loop over all the soundfont presets
    for (auto& preset : m_soundfont.presets) {
        // Get the bank and program for the current one
        const auto bank = (preset.first & 0xFF00) >> 8;
        const auto program = (preset.first & 0x00FF);

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
        m_preset_dropdown->list_items.push_back(name);

        // Add the index to the indices map, so we can update dropdown menu when the bank/program numberboxes update
        m_dropdown_indices[preset.first] = static_cast<int>(m_dropdown_indices_inverse.size());

        // Add the bank and program to the inverse list, so we can update the bank and program number boxes when the dropdown menu updates
        m_dropdown_indices_inverse.push_back(preset.first);

    }
}

void FlanSoundfontPlayer::load_soundfont(const std::string& path) {
    // Load soundfont
    m_soundfont.clear();
    m_soundfont.from_file(path);

    // Get text in the browse box
    wchar_t* text_soundfont_path = reinterpret_cast<wchar_t*>(scene.value_pool.values["text_soundfont_path"]);

    // Reallocate it for the new path length
    text_soundfont_path = static_cast<wchar_t*>(realloc(text_soundfont_path, (path.size() + 1) * 2));

    // Copy the string to it
    for (size_t i = 0; i < path.size() + 1; ++i) {
        text_soundfont_path[i] = path[i];
    }

    // Set the text in the browse box
    scene.value_pool.set_value("text_soundfont_path", reinterpret_cast<intptr_t>(text_soundfont_path));

    // Update the dropdown menu
    update_preset_dropdown_menu();
}
