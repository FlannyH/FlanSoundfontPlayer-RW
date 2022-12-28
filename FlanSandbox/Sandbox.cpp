#include "../FlanGUI/Renderer.h"
#include "../FlanGUI/ComponentSystem.h"
#include "../FlanGUI/ComponentsGUI.h"
#include "../FlanGUI/Input.h"
#include "../FlanGUI/External/include/glfw/glfw3.h"

static std::chrono::time_point<std::chrono::steady_clock> start = std::chrono::steady_clock::now();
static std::chrono::time_point<std::chrono::steady_clock> end = std::chrono::steady_clock::now();

static float calculate_delta_time()
{
    end = std::chrono::steady_clock::now();
    const std::chrono::duration<float> delta = end - start;
    start = std::chrono::steady_clock::now();
    return delta.count();
}

int main()
{
    Flan::Renderer renderer;
    Flan::Scene scene;
    renderer.init();
    Flan::Input input(renderer.window());

    float smooth_dt = 0.0f;
    [[maybe_unused]] float time = 0.0f;
    wchar_t frametime_text[512];

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
        Flan::create_numberbox(scene, "program", nb_program_transform, nb_program_number_range);
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
        Flan::create_combobox(scene, "combobox_preset", db_program_transform, { L"000:000 - Piano 1", L"000:001 - Piano 2" });
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
            }, { L"Relative volume envelope overrides:", {2, 2}, {1, 1, 1, 1}, Flan::AnchorPoint::left, Flan::AnchorPoint::left}, false);
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

    //--------------------------


    while (!glfwWindowShouldClose(renderer.window())) {
        // Draw
        renderer.begin_frame();
        const float dt = calculate_delta_time();
        time += dt;
        smooth_dt = smooth_dt + (dt - smooth_dt) * (1.f - powf(0.02f, dt));
        Flan::update_entities(scene, renderer, input, dt);

        renderer.end_frame();
        input.update(renderer.window());
    }
}