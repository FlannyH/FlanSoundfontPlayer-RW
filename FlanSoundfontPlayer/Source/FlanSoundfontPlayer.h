#pragma once
#include <thread>
#include <fstream>
#include <iostream>
#include "FruityPlug/fp_cplug.h"
#include "../../FlanGUI/Renderer.h"
#include "../../FlanGUI/ComponentSystem.h"
#include "../../FlanGUI/ComponentsGUI.h"
#include "../../SoundfontStudies/SoundfontStudies/soundfont.h"
#include "WavetableOscillator.h"
#define N_WAVE_OSCS 64

class FlanSoundfontPlayer : public TCPPFruityPlug
{
public:
    FlanSoundfontPlayer(int set_tag, TFruityPlugHost* host);
    ~FlanSoundfontPlayer();
    intptr_t _stdcall Dispatcher(intptr_t id, intptr_t index, intptr_t value) override;
    TVoiceHandle _stdcall TriggerVoice(PVoiceParams voice_params, intptr_t set_tag) override;
    void _stdcall Voice_Release(TVoiceHandle handle) override;
    int _stdcall ProcessEvent(int event_id, int event_value, int flags) override;
    void _stdcall Gen_Render(PWAV32FS dest_buffer, int& length) override;
    
    void _stdcall Idle() override;
    Flan::Renderer renderer;
    Flan::Scene scene;
    Flan::Input* input = nullptr;
    bool window_safe = false;
    bool not_destructing = true;
   
private:
    // UI
    void CreateUI();
    void UpdatePresetDropdownMenu();
    GLFWwindow* gl_window;
    std::thread update_render_thread;
    Flan::Combobox* preset_dropdown = nullptr;

    // Soundfont
    Flan::Soundfont soundfont;

    // Voices
    Flan::WavetableOscillator wave_oscs[N_WAVE_OSCS]{};
    std::vector<Flan::WavetableOscillator*> active_wave_oscs;
    size_t curr_wave_osc_idx = 0;

    // Optimizations
    std::vector<u16> dropdown_indices_inverse;
    std::map<u16, int> dropdown_indices;

    // Debug
    wchar_t debug_buffer[1024] = { 0 };
};

