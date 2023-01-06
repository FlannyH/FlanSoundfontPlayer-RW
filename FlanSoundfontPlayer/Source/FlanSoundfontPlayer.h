#pragma once
#include <thread>
#include <mutex>

#include "Scale.h"
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
    int _stdcall ProcessParam(int index, int value, int rec_flags) override;
    void _stdcall GetName(int section, int index, int value, char* name) override;
    
    void _stdcall Idle() override;
    Flan::Renderer renderer;
    Flan::Scene scene;
    Flan::Input* input = nullptr;
    Flan::Scale scale;
    bool window_safe = false;
    bool not_destructing = true;
    std::mutex graphics_thread_lock;
    std::string soundfont_to_load;
    void load_soundfont(const std::string& path);
    float calculate_delta_time();

private:
    // UI
    void create_ui();
    void update_preset_dropdown_menu();
    std::thread m_update_render_thread;
    Flan::Combobox* m_preset_dropdown = nullptr;

    // Soundfont
    Flan::Soundfont m_soundfont;

    // Voices
    std::vector<Flan::Voice*> m_active_voices;
    std::mutex m_note_playing_mutex;
    double m_midi_pitch = 0.0;
    double m_sample_rate = 1.0;
    double m_sample_rate_inv = 1.0;

    // Optimizations
    std::vector<u16> m_dropdown_indices_inverse;
    std::map<u16, int> m_dropdown_indices;

    // Delta Time
    std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
    std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();

    // Debug
    wchar_t m_debug_buffer[1024] = { 0 };
};