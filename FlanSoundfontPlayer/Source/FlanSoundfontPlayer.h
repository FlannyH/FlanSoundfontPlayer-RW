#pragma once
#include <thread>
#include <fstream>
#include <iostream>
#include <mutex>

#include "FruityPlug/fp_cplug.h"
#include "../../FlanGUI/Renderer.h"
#include "../../FlanGUI/ComponentSystem.h"
#include "../../FlanGUI/ComponentsGUI.h"
#include "../../SoundfontStudies/SoundfontStudies/soundfont.h"
#include "WavetableOscillator.h"
#define N_WAVE_OSCS 64

class FlanSoundfontPlayer final : public TCPPFruityPlug
{
public:
    FlanSoundfontPlayer(int set_tag, TFruityPlugHost* host);
    ~FlanSoundfontPlayer() override;
    intptr_t _stdcall Dispatcher(intptr_t id, intptr_t index, intptr_t value) override;
    TVoiceHandle _stdcall TriggerVoice(PVoiceParams voice_params, intptr_t set_tag) override;
    void _stdcall Voice_Release(TVoiceHandle handle) override;
    int _stdcall ProcessEvent(int event_id, int event_value, int flags) override;
    void _stdcall Gen_Render(PWAV32FS dest_buffer, int& length) override;
    void _stdcall SaveRestoreState(IStream* stream, BOOL save) override;
    
    void _stdcall Idle() override;
    Flan::Renderer renderer;
    Flan::Scene scene;
    Flan::Input* input = nullptr;
    bool window_safe = false;
    bool not_destructing = true;
    std::mutex graphics_thread_lock;
    std::string soundfont_to_load;
    void load_soundfont(const std::string& path);

private:
    // UI
    void create_ui();
    void update_preset_dropdown_menu();
    std::thread m_update_render_thread;
    Flan::Combobox* m_preset_dropdown = nullptr;

    // Soundfont
    Flan::Soundfont m_soundfont;

    // Voices
    [[deprecated]] Flan::WavetableOscillator m_wave_oscs[N_WAVE_OSCS]{};
    std::vector<Flan::Voice*> m_active_voices;
    [[deprecated]] size_t m_curr_wave_osc_idx = 0;
    std::mutex m_note_playing_mutex;
    float m_midi_pitch = 0.0f;
    float m_sample_rate = 1.0f;
    float m_sample_rate_inv = 1.0f;

    // Optimizations
    std::vector<u16> m_dropdown_indices_inverse;
    std::map<u16, int> m_dropdown_indices;

    // Debug
    wchar_t m_debug_buffer[1024] = { 0 };
};

