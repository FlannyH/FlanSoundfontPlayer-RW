#pragma once
#include "FruityPlug/fp_cplug.h"
#include "../../FlanGUI/Renderer.h"
#include "../../FlanGUI/ComponentSystem.h"
#include "../../FlanGUI/ComponentsGUI.h"
#include <thread>
#include <fstream>
#include <iostream>

class FlanSoundfontPlayer : public TCPPFruityPlug
{
public:
    FlanSoundfontPlayer(int set_tag, TFruityPlugHost* host);
    ~FlanSoundfontPlayer();
    intptr_t _stdcall Dispatcher(intptr_t id, intptr_t index, intptr_t value) override;
    void _stdcall Idle() override;
    Flan::Renderer renderer;
    Flan::Scene scene;
    Flan::Input* input = nullptr;
    bool window_safe = false;
    bool not_destructing = true;
   
private:
    GLFWwindow* gl_window;
    std::thread update_render_thread;
};

