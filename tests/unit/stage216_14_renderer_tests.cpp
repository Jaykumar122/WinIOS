#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "renderer/d3d11_renderer.h"
#include "renderer/presentation_pacer.h"
#include "renderer/renderer.h"
#include "renderer/software_renderer.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

using namespace winios::renderer;

void test_software_renderer_pipeline() {
    SoftwareRenderer renderer;
    TEST_ASSERT(renderer.initialize(nullptr, 200, 100));
    TEST_ASSERT(renderer.backend_name() == "SoftwareReference");
    TEST_ASSERT(!renderer.is_hardware_accelerated());

    // Create 100x50 test pattern (ARGB blue)
    std::vector<std::uint32_t> test_pixels(100 * 50, 0xFFFF0000); // ARGB blue in B8G8R8A8

    TEST_ASSERT(renderer.begin_frame());
    TEST_ASSERT(renderer.submit_frame(reinterpret_cast<const std::uint8_t*>(test_pixels.data()), 100, 50, 100 * 4));
    TEST_ASSERT(renderer.end_frame(false));

    auto diag = renderer.diagnostics();
    TEST_ASSERT(diag.total_frames_submitted == 1);
    TEST_ASSERT(diag.total_frames_presented == 1);
    TEST_ASSERT(diag.dropped_frames == 0);

    // Verify stretched backbuffer has test color
    const auto& bb = renderer.backbuffer();
    TEST_ASSERT(!bb.empty());
    std::uint32_t pixel = *reinterpret_cast<const std::uint32_t*>(bb.data());
    TEST_ASSERT(pixel == 0xFFFF0000);

    // Test resize
    TEST_ASSERT(renderer.resize(400, 200));
    TEST_ASSERT(renderer.diagnostics().width == 400);

    // Test device loss simulation and recovery
    renderer.simulate_device_loss();
    TEST_ASSERT(!renderer.begin_frame()); // Should drop frames during loss
    TEST_ASSERT(renderer.diagnostics().device_loss_events == 1);

    TEST_ASSERT(renderer.recover_from_device_loss());
    TEST_ASSERT(renderer.diagnostics().recovery_events == 1);
    TEST_ASSERT(renderer.begin_frame()); // Back to normal

    std::cout << "[PASS] test_software_renderer_pipeline passed\n";
}

void test_d3d11_renderer_lifecycle_and_fallback() {
    D3D11Renderer renderer;
    // Without HWND (headless), must initialize gracefully via software fallback
    TEST_ASSERT(renderer.initialize(nullptr, 300, 200));

    std::vector<std::uint32_t> test_pixels(300 * 200, 0xFF00FF00); // Green

    TEST_ASSERT(renderer.begin_frame());
    TEST_ASSERT(renderer.submit_frame(reinterpret_cast<const std::uint8_t*>(test_pixels.data()), 300, 200, 300 * 4));
    TEST_ASSERT(renderer.end_frame(false));

    auto diag = renderer.diagnostics();
    TEST_ASSERT(diag.total_frames_presented == 1);

    // Test device loss simulation & recovery
    renderer.simulate_device_loss();
    TEST_ASSERT(!renderer.begin_frame());
    TEST_ASSERT(renderer.diagnostics().device_loss_events == 1);

    TEST_ASSERT(renderer.recover_from_device_loss());
    TEST_ASSERT(renderer.diagnostics().recovery_events == 1);
    TEST_ASSERT(renderer.begin_frame());

    std::cout << "[PASS] test_d3d11_renderer_lifecycle_and_fallback passed\n";
}

void test_presentation_pacing_and_diagnostics() {
    PresentationPacer pacer;

    // Simulate 3 frames presented at 16.6ms intervals (60 FPS)
    std::uint64_t t0 = 1'000'000'000ULL; // 1s
    pacer.on_frame_presented(t0);

    std::uint64_t t1 = t0 + 16'666'667ULL; // +16.66ms
    pacer.on_frame_presented(t1);

    std::uint64_t t2 = t1 + 16'666'667ULL; // +16.66ms
    pacer.on_frame_presented(t2);

    pacer.on_frame_dropped();

    auto diag = pacer.diagnostics();
    TEST_ASSERT(diag.total_frames == 3);
    TEST_ASSERT(diag.dropped_frames == 1);
    TEST_ASSERT(diag.fps >= 58.0 && diag.fps <= 62.0);
    TEST_ASSERT(diag.average_interval_ms >= 16.0 && diag.average_interval_ms <= 17.5);

    std::cout << "[PASS] test_presentation_pacing_and_diagnostics passed\n";
}

int main() {
    std::cout << "=== Running Stage 216.14 Renderer Architecture Tests ===" << std::endl;
    test_software_renderer_pipeline();
    test_d3d11_renderer_lifecycle_and_fallback();
    test_presentation_pacing_and_diagnostics();
    std::cout << "=== All Stage 216.14 Renderer Tests PASSED ===" << std::endl;
    return 0;
}
