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
    Flan::Transform text_bank_transform{
        {20, 20},
        {100, 40},
        0.5f,
        Flan::AnchorPoint::top_left
    };
    Flan::create_text(scene, "text_bank", text_bank_transform, {
        L"Bank:",
        {2, 2},
        {1, 1, 1, 1},
        Flan::AnchorPoint::center,
        Flan::AnchorPoint::top_left
        });

    // Create bank numberbox
    Flan::Transform nb_bank_transform {
        {20, 40},
        {100, 120},
        0.5f,
        Flan::AnchorPoint::top_left
    };
    Flan::NumberRange nb_bank_number_range{ 0, 127, 1 };
    Flan::create_numberbox(scene, "bank", nb_bank_transform, nb_bank_number_range, 0);

    //--------------------------


    while (!glfwWindowShouldClose(renderer.window())) {
        // Draw
        renderer.begin_frame();
        const float dt = calculate_delta_time();
        time += dt;
        smooth_dt = smooth_dt + (dt - smooth_dt) * (1.f - powf(0.02f, dt));
        swprintf_s(frametime_text, L"frametime: %.5f ms\nframe rate: %.3f fps\nmouse_pos_absolute: %.0f, %.0f\nmouse_pos_window: %.0f, %.0f\nmouse_pos_relative: %.0f, %.0f\nmouse_buttons = %i%i%i\nmouse_down = %i%i%i\nmouse_up = %i%i%i\nmouse_wheel = %.0f\ndebug_numberbox = %f\ndebug_radio_button = %f\ndebug_combobox = %f\n",
            smooth_dt * 1000.f,
            1.0f / smooth_dt,
            input.mouse_pos(Flan::MouseRelative::absolute).x,
            input.mouse_pos(Flan::MouseRelative::absolute).y,
            input.mouse_pos(Flan::MouseRelative::window).x,
            input.mouse_pos(Flan::MouseRelative::window).y,
            input.mouse_pos(Flan::MouseRelative::relative).x,
            input.mouse_pos(Flan::MouseRelative::relative).y,
            input.mouse_held(0), input.mouse_held(1), input.mouse_held(2),
            input.mouse_down(0), input.mouse_down(1), input.mouse_down(2),
            input.mouse_up(0), input.mouse_up(1), input.mouse_up(2),
            input.mouse_wheel(),
            Flan::Value::get<double>("debug_numberbox"),
            Flan::Value::get<double>("debug_radio_button"),
            Flan::Value::get<double>("debug_combobox")
        );
        Flan::Value::set_ptr("debug_text", &frametime_text);
        Flan::update_entities(scene, renderer, input, dt);

        renderer.end_frame();
        input.update(renderer.window());
    }
}