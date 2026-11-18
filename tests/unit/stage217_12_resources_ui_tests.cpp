#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "resources/resource_bundle.h"
#include "resources/resource_manager.h"
#include "runtime/ui/font_engine.h"
#include "runtime/ui/guest_ui_framework.h"
#include "runtime/ui/guest_layout_engine.h"
#include "runtime/app_lifecycle.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::resources;
using namespace winios::runtime;
using namespace winios::runtime::ui;

void test_resource_bundle_and_manager() {
    auto bundle = std::make_shared<ResourceBundle>();

    ResourceItem txt_item{};
    txt_item.id = "greeting";
    txt_item.type = ResourceType::kText;
    std::string text_val = "Welcome to WinIOS!";
    txt_item.data.assign(text_val.begin(), text_val.end());
    TEST_ASSERT(bundle->AddResource(txt_item));

    // Valid 2x2 24-bit BMP image
    ResourceItem bmp_item{};
    bmp_item.id = "icon";
    bmp_item.type = ResourceType::kImage;
    bmp_item.data = {
        0x42, 0x4D, 0x3A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00, 0x00, 0x00,
        0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00
    };
    TEST_ASSERT(bundle->AddResource(bmp_item));

    ResourceManager mgr(bundle, 10);
    std::string loaded_txt;
    TEST_ASSERT(mgr.LoadText("greeting", &loaded_txt));
    TEST_ASSERT(loaded_txt == text_val);

    DecodedImage img{};
    TEST_ASSERT(mgr.LoadImage("icon", &img));
    TEST_ASSERT(img.is_valid);
    TEST_ASSERT(img.width == 2 && img.height == 2);
    TEST_ASSERT(img.pixels_argb.size() == 4);

    std::cout << "[PASS] test_resource_bundle_and_manager\n";
}

void test_font_engine_and_ui_framework() {
    auto& font = FontEngine::Get();
    auto metrics = font.MeasureText("Hello", 14.0f);
    TEST_ASSERT(metrics.width == 40);
    TEST_ASSERT(metrics.height == 16);

    std::vector<uint32_t> surface(200 * 100, WINIOS_COLOR_WHITE);
    font.RenderText("Test", 10, 10, WINIOS_COLOR_BLACK, surface.data(), 200, 100);

    // Test UI controls
    GuestContainer container;
    container.SetFrame(0, 0, 200, 100);

    auto btn = std::make_shared<GuestButton>("OK");
    btn->SetFrame(10, 10, 60, 30);
    bool clicked = false;
    btn->SetOnClick([&clicked] { clicked = true; });

    auto input = std::make_shared<GuestTextInput>("Type...");
    input->SetFrame(10, 50, 100, 30);

    container.AddChild(btn);
    container.AddChild(input);

    // Click button
    winios_event_t ev_down{};
    ev_down.type = WINIOS_EVENT_TOUCH_BEGIN;
    ev_down.x = 20;
    ev_down.y = 20;
    TEST_ASSERT(container.HandleEvent(ev_down));

    winios_event_t ev_up{};
    ev_up.type = WINIOS_EVENT_TOUCH_END;
    ev_up.x = 20;
    ev_up.y = 20;
    TEST_ASSERT(container.HandleEvent(ev_up));
    TEST_ASSERT(clicked);

    // Focus input and type
    winios_event_t ev_tap_input{};
    ev_tap_input.type = WINIOS_EVENT_TOUCH_BEGIN;
    ev_tap_input.x = 20;
    ev_tap_input.y = 60;
    TEST_ASSERT(container.HandleEvent(ev_tap_input));
    TEST_ASSERT(input->IsFocused());

    winios_event_t ev_char{};
    ev_char.type = WINIOS_EVENT_KEY_CHAR;
    ev_char.key_code = 'A';
    TEST_ASSERT(container.HandleEvent(ev_char));
    TEST_ASSERT(input->GetText() == "A");

    // Layout engine test
    LayoutConstraints constraints{};
    constraints.padding_left = 5;
    constraints.padding_top = 5;
    constraints.spacing = 10;
    GuestLayoutEngine::ArrangeLinear(container, LayoutDirection::kColumn, constraints);

    TEST_ASSERT(btn->GetFrame().x == 5);
    TEST_ASSERT(btn->GetFrame().y == 5);
    TEST_ASSERT(input->GetFrame().x == 5);
    TEST_ASSERT(input->GetFrame().y == 5 + 30 + 10);

    // Render container
    container.Render(surface.data(), 200, 100);

    std::cout << "[PASS] test_font_engine_and_ui_framework\n";
}

class TestLifecycleObserver : public AppLifecycleObserver {
public:
    int launch_count{0};
    int active_count{0};
    int bg_count{0};
    int terminate_count{0};

    void OnLaunch() override { launch_count++; }
    void OnActive() override { active_count++; }
    void OnBackground() override { bg_count++; }
    void OnTerminate() override { terminate_count++; }
};

void test_app_lifecycle() {
    AppLifecycleManager mgr(10);
    TestLifecycleObserver obs;
    mgr.RegisterObserver(&obs);

    TEST_ASSERT(mgr.GetCurrentState() == AppLifecycleState::kUnloaded);
    TEST_ASSERT(mgr.TransitionTo(AppLifecycleState::kLaunching));
    TEST_ASSERT(obs.launch_count == 1);

    TEST_ASSERT(mgr.TransitionTo(AppLifecycleState::kActive));
    TEST_ASSERT(obs.active_count == 1);

    TEST_ASSERT(mgr.TransitionTo(AppLifecycleState::kBackgrounded));
    TEST_ASSERT(obs.bg_count == 1);

    TEST_ASSERT(mgr.TransitionTo(AppLifecycleState::kActive));
    TEST_ASSERT(obs.active_count == 2);

    TEST_ASSERT(mgr.TransitionTo(AppLifecycleState::kTerminated));
    TEST_ASSERT(obs.terminate_count == 1);

    // Terminal state cannot transition
    TEST_ASSERT(!mgr.TransitionTo(AppLifecycleState::kActive));

    std::cout << "[PASS] test_app_lifecycle\n";
}

int main() {
    test_resource_bundle_and_manager();
    test_font_engine_and_ui_framework();
    test_app_lifecycle();
    std::cout << "All Stage 217.12-217.18 resource and UI framework tests passed.\n";
    return 0;
}
