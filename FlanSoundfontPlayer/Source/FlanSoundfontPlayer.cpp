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
    if (reason == DLL_PROCESS_ATTACH) dll_handle = module;
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
        AudioRenderer.setSmpRate(value);
        PitchMul = (float)(MiddleCMul / AudioRenderer.getSmpRate());
        break;
    }

    return r;
    return 0;
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
        Flan::create_numberbox(scene, "bank", nb_bank_transform, nb_bank_number_range);
    }
    // Create bank text
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
    // Create bank numberbox
    {
        Flan::Transform nb_program_transform{
            {180, 60},
            {340, 140},
            0.5f,
            Flan::AnchorPoint::top_left
        };
        Flan::NumberRange nb_program_number_range{ 0, 127, 1, 0, 0 };
        Flan::EntityID entity = Flan::create_numberbox(scene, "program", nb_program_transform, nb_program_number_range);
        Flan::add_function(scene, entity, []()
            {
                printf("bank value changed!\n");
            }
        );
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
}

void FlanSoundfontPlayer::UpdatePresetDropdownMenu()
{
    // Clear the list of presets
    preset_dropdown->list_items.clear();
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
    }
}
