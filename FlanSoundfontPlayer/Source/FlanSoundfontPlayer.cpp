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
    // Allocate the plugin info struct
    Info = &plug_info;
    EditorHandle = glfwGetWin32Window(renderer.window());
    update_render_thread = std::thread(UpdateRender, this);


    input = new Flan::Input(renderer.window());

    Flan::create_combobox(scene, "debug_combobox", { {800, 20}, {1200, 80}, 0.01f }, std::vector<std::wstring>({
        L"Option 1",
        L"Option 2",
        L"Option 3",
        L"Option 4",
        L"Option 5",
        L"Option 6",
        L"Option 7",
        L"Option 8",
        L"Option 9",
        L"Option A",
        L"Option B",
        L"Option C",
        L"Option D",
        L"Option E",
        L"Option F",
        })
        );

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
            renderer.init(1280, 720, true);
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
