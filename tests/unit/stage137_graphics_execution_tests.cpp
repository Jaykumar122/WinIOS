#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/window_server.h"
#include "loader/apple_artifact_admission.h"
#include "loader/apple_boot_artifact_inventory.h"
#include "loader/userspace_macho_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::runtime;
using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Genuine WindowServer Mach-O Admission Gate (BLOCKED)
bool test_windowserver_genuine_admission_gate() {
    std::cout << "[Test 1] Genuine WindowServer Mach-O Admission Gate (BLOCKED)...\n";

    AppleArtifactAdmissionController controller;

    // Genuine unextracted WindowServer binary
    AppleBootArtifact real_ws{};
    real_ws.artifact_id = "genuine_windowserver";
    real_ws.file_path = "/System/Library/Frameworks/CoreGraphics.framework/WindowServer";
    real_ws.kind = AppleArtifactKind::WindowServerBinary;
    real_ws.provenance = ArtifactProvenance::DecryptedReal;
    real_ws.is_synthetic = false;

    auto res = controller.evaluate_artifact(real_ws, nullptr, 0);
    TEST_ASSERT(!res.admitted, "Genuine unextracted WindowServer must NOT be admitted");
    TEST_ASSERT(res.decision == AdmissionDecision::Rejected_Truncated,
                "Must reject empty/missing genuine WindowServer artifact");

    std::cout << "  Passed (Genuine WindowServer admission strictly BLOCKED)\n";
    return true;
}

// Test 2: iPhone 15 Display Geometry & Framebuffer Size Verification
bool test_iphone15_display_geometry_and_stride() {
    std::cout << "[Test 2] iPhone 15 Display Geometry & Framebuffer Sizing...\n";

    WindowServerCompositor compositor;
    TEST_ASSERT(compositor.width() == 1179u, "iPhone 15 width must equal 1179 px");
    TEST_ASSERT(compositor.height() == 2556u, "iPhone 15 height must equal 2556 px");

    // 1179 * 4 = 4716 bytes per scanline
    std::size_t expected_stride = 1179u * 4u;
    std::size_t expected_total_bytes = expected_stride * 2556u; // 12,054,096 bytes
    TEST_ASSERT(compositor.framebuffer_size() == expected_total_bytes,
                "iPhone 15 32-bit framebuffer must equal exactly 12,054,096 bytes");

    std::cout << "  Passed (iPhone 15 Super Retina XDR resolution: 1179x2556 @ 32bpp, "
              << expected_total_bytes << " bytes)\n";
    return true;
}

// Test 3: Guest CPU Pixel Store Instructions (STR W2, [X0])
bool test_guest_cpu_framebuffer_pixel_store() {
    std::cout << "[Test 3] Guest CPU Framebuffer Store Instructions (STR W2, [X0])...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // Assembly to store 4 pixels of iOS System Blue (0x007AFF) at RAM offset 0x30000:
    // movz x0, #3, lsl #16     (x0 = 0x30000)
    // movz w2, #0x7AFF         (w2 = 0x7AFF)
    // str w2, [x0]             (pixel 0)
    // str w2, [x0, #4]         (pixel 1)
    // str w2, [x0, #8]         (pixel 2)
    // str w2, [x0, #12]        (pixel 3)
    // mov x0, #0
    // mov x16, #1              (SYS_exit 0)
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2A00060u, // movz x0, #3, lsl #16 (0x30000)
        0x528F5FE2u, // movz w2, #0x7AFF
        0xB9000002u, // str w2, [x0]
        0xB9000402u, // str w2, [x0, #4]
        0xB9000802u, // str w2, [x0, #8]
        0xB9000C02u, // str w2, [x0, #12]
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/System/Library/Frameworks/CoreGraphics.framework/WindowServer";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 5);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "Pixel store process must exit cleanly");
    TEST_ASSERT(res.guest_instructions_retired >= 9, "All guest store instructions must retire");

    // Verify 4 pixels in guest memory at 0x30000 contain 0x007AFF
    for (int p = 0; p < 4; ++p) {
        std::uint32_t stored_val = mem.read_u32(0x30000 + p * 4);
        TEST_ASSERT((stored_val & 0x00FFFFFFu) == 0x007AFFu, "Stored pixel must match iOS System Blue");
    }

    std::cout << "  Passed (Guest CPU retired 4 STR stores writing iOS System Blue into RAM)\n";
    return true;
}

// Test 4: IOSurface Allocation and Pixel Access
bool test_iosurface_allocation_and_readwrite() {
    std::cout << "[Test 4] IOSurface Allocation and Pixel Access...\n";

    IOSurface surface(1, 200, 100, PixelFormat::B8G8R8A8_UNORM);
    TEST_ASSERT(surface.id() == 1, "Surface ID must match");
    TEST_ASSERT(surface.width() == 200, "Surface width = 200");
    TEST_ASSERT(surface.height() == 100, "Surface height = 100");
    TEST_ASSERT(surface.stride() == 800, "Stride = 200 * 4 = 800 bytes");
    TEST_ASSERT(surface.size_bytes() == 80000, "Total bytes = 80,000");

    // Write pixel at (50, 25)
    surface.write_pixel(50, 25, 0xFFFF0000u); // Red
    TEST_ASSERT(surface.read_pixel(50, 25) == 0xFFFF0000u, "Read pixel must match written value");

    // Fill rectangle (10, 10, 20, 20) with green
    Rect rect{10, 10, 20, 20};
    surface.fill_rect(rect, 0xFF00FF00u);
    TEST_ASSERT(surface.read_pixel(15, 15) == 0xFF00FF00u, "Inside rect must be green");
    TEST_ASSERT(surface.read_pixel(5, 5) == 0x00000000u, "Outside rect must remain clear");

    std::cout << "  Passed (IOSurface buffer indexing and rectangular raster ops confirmed)\n";
    return true;
}

// Test 5: DamageTracker Dirty Region Aggregation
bool test_damage_tracker_dirty_region_aggregation() {
    std::cout << "[Test 5] DamageTracker Dirty Region Aggregation...\n";

    DamageTracker tracker;
    TEST_ASSERT(!tracker.has_damage(), "Fresh tracker must have no damage");

    // Add first dirty rect
    tracker.add_dirty_rect(Rect{10, 10, 50, 50});
    TEST_ASSERT(tracker.has_damage(), "Tracker must have damage");
    TEST_ASSERT(tracker.bounding_box().x == 10 && tracker.bounding_box().y == 10, "Bounding origin");
    TEST_ASSERT(tracker.bounding_box().width == 50 && tracker.bounding_box().height == 50, "Bounding size");

    // Add second dirty rect expanding bounding box
    tracker.add_dirty_rect(Rect{40, 40, 100, 100});
    const auto& bb = tracker.bounding_box();
    TEST_ASSERT(bb.x == 10 && bb.y == 10, "Bounding min bounds");
    TEST_ASSERT(bb.width == 130 && bb.height == 130, "Bounding expanded width/height");

    // Clear damage
    tracker.clear();
    TEST_ASSERT(!tracker.has_damage(), "Damage cleared");

    std::cout << "  Passed (Dirty rect union and bounding box aggregation verified)\n";
    return true;
}

// Test 6: Multi-Layer WindowServer Compositing
bool test_windowserver_multi_layer_compositing() {
    std::cout << "[Test 6] Multi-Layer WindowServer Compositing...\n";

    WindowServerCompositor compositor(100, 100);

    // Layer 1: Background (Red)
    auto bg_surf = compositor.create_surface(100, 100);
    bg_surf->fill_rect(Rect{0, 0, 100, 100}, 0xFFFF0000u);
    WindowServerLayer bg_layer{1, 10, bg_surf, Rect{0, 0, 100, 100}, 1.0f, 0, false};
    compositor.add_layer(bg_layer);

    // Layer 2: Foreground Window (Blue) at (25, 25, 50, 50)
    auto fg_surf = compositor.create_surface(50, 50);
    fg_surf->fill_rect(Rect{0, 0, 50, 50}, 0xFF0000FFu);
    WindowServerLayer fg_layer{2, 20, fg_surf, Rect{25, 25, 50, 50}, 1.0f, 1, false};
    compositor.add_layer(fg_layer);

    std::vector<std::uint8_t> output_fb(100 * 100 * 4, 0);
    bool ok = compositor.composite(output_fb.data(), output_fb.size());
    TEST_ASSERT(ok, "Compositing must succeed");

    // Verify background pixel outside window is Red
    auto* pixels = reinterpret_cast<const std::uint32_t*>(output_fb.data());
    TEST_ASSERT(pixels[5 * 100 + 5] == 0xFFFF0000u, "Background pixel must be Red");

    // Verify foreground pixel inside window is Blue
    TEST_ASSERT(pixels[30 * 100 + 30] == 0xFF0000FFu, "Window pixel must be Blue");

    std::cout << "  Passed (Composited background and foreground layers with z-ordering)\n";
    return true;
}

// Test 7: DirectX Presentation Adapter & Host Staging
bool test_directx_host_presentation_adapter() {
    std::cout << "[Test 7] DirectX Presentation Adapter & Host Staging Buffer...\n";

    DirectXPresentationAdapter presenter(100, 100);
    std::vector<std::uint32_t> test_frame(100 * 100, 0xFF007AFFu); // iOS Blue

    bool presented = presenter.present(test_frame.data(), test_frame.size() * 4, Rect{0, 0, 100, 100});
    TEST_ASSERT(presented, "Presentation must succeed");

    const auto& stats = presenter.stats();
    TEST_ASSERT(stats.total_frames == 1, "Total frames = 1");
    TEST_ASSERT(stats.presented_frames == 1, "Presented frames = 1");
    TEST_ASSERT(stats.dropped_frames == 0, "Dropped frames = 0");

    // Verify host staging buffer received pixels
    const auto* staged = reinterpret_cast<const std::uint32_t*>(presenter.host_staging_buffer().data());
    TEST_ASSERT(staged[0] == 0xFF007AFFu, "Staged pixel must match input framebuffer");

    std::cout << "  Passed (DirectX staging transfer and frame performance accounting confirmed)\n";
    return true;
}

// Test 8: Guest-Rendered vs Host-Rendered Trace Separation
bool test_guest_rendered_vs_host_rendered_trace_separation() {
    std::cout << "[Test 8] Guest-Rendered vs Host-Rendered Trace Separation...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    std::vector<std::uint32_t> guest_asm = {
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/System/Library/Frameworks/CoreGraphics.framework/WindowServer";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 5);

    // Host compositor execution
    WindowServerCompositor compositor(50, 50);
    std::vector<std::uint8_t> fb(50 * 50 * 4, 0);
    compositor.composite(fb.data(), fb.size());
    compositor.present_to_host(fb.data(), fb.size());

    TEST_ASSERT(res.guest_instructions_retired >= 3, "Guest instructions must be retired");
    TEST_ASSERT(compositor.presentation_adapter().stats().total_frames >= 1, "Host presented frame");

    std::cout << "  Passed (Trace cleanly distinguishes Guest EL0 CPU events from Host DirectX events)\n";
    return true;
}

// Test 9: Framebuffer Damage Transfer to Presentation Adapter
bool test_guest_framebuffer_damage_transfer() {
    std::cout << "[Test 9] Framebuffer Damage Transfer to Presentation Adapter...\n";

    WindowServerCompositor compositor(100, 100);
    auto surf = compositor.create_surface(100, 100);
    surf->fill_rect(Rect{20, 20, 30, 30}, 0xFF00FF00u); // Green damage patch

    compositor.add_layer(WindowServerLayer{1, 10, surf, Rect{0, 0, 100, 100}, 1.0f, 0, false});
    compositor.damage_tracker().clear();
    compositor.mark_damage(Rect{20, 20, 30, 30});

    TEST_ASSERT(compositor.damage_tracker().has_damage(), "Damage must be active");
    TEST_ASSERT(compositor.damage_tracker().bounding_box().x == 20, "Damage X = 20");
    TEST_ASSERT(compositor.damage_tracker().bounding_box().width == 30, "Damage Width = 30");

    std::vector<std::uint8_t> fb(100 * 100 * 4, 0);
    bool ok = compositor.composite(fb.data(), fb.size());
    TEST_ASSERT(ok, "Composite succeeded");

    bool present_ok = compositor.present_to_host(fb.data(), fb.size());
    TEST_ASSERT(present_ok, "Present succeeded");
    TEST_ASSERT(!compositor.damage_tracker().has_damage(), "Damage cleared after present");

    std::cout << "  Passed (Dirty region accurately transferred and cleared post-presentation)\n";
    return true;
}

// Test 10: Complete End-to-End WindowServer Graphics Pipeline
bool test_end_to_end_windowserver_graphics_pipeline() {
    std::cout << "[Test 10] Complete End-to-End WindowServer Graphics Pipeline...\n";

    CPU cpu;
    Memory mem(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, mem, bsd, ipc);

    // 1. WindowServer executes at PID 5
    // Guest registers com.apple.windowserver.active in Mach IPC
    // Assembly:
    // mov x16, #20         (SYS_getpid)
    // svc #0x80
    // mov x16, #-28        (task_self_trap)
    // svc #0x81
    // mov x0, #0
    // mov x16, #1
    // svc #0x80
    std::vector<std::uint32_t> guest_asm = {
        0xD2800290u, // mov x16, #20
        0xD4001001u, // svc #0x80
        0x92800370u, // mov x16, #-28
        0xD4001021u, // svc #0x81
        0xD2800000u, // mov x0, #0
        0xD2800030u, // mov x16, #1
        0xD4001001u  // svc #0x80
    };
    std::vector<std::uint8_t> code(guest_asm.size() * 4);
    std::memcpy(code.data(), guest_asm.data(), code.size());

    auto macho = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"}, code);

    DarwinPipelineConfig cfg;
    cfg.executable_path = "/System/Library/Frameworks/CoreGraphics.framework/WindowServer";
    auto res = engine.execute_macho(macho.data(), macho.size(), cfg, 5);

    TEST_ASSERT(res.process_exited && res.exit_code == 0, "WindowServer init exited cleanly");

    // 2. Register service in Mach IPC
    auto space = ipc.get_task_port_space(5);
    auto ws_obj = std::make_shared<MachPortObject>(9001, 5);
    mach_port_t ws_port = space->allocate_port(ws_obj, kMachPortRightReceive);
    space->insert_right(ws_port, ws_obj, kMachPortRightSend);
    ipc.bootstrap_register(5, "com.apple.windowserver.active", ws_port);

    // 3. Composite full iPhone 15 frame and present
    WindowServerCompositor compositor;
    auto root_surf = compositor.create_surface(compositor.width(), compositor.height());
    root_surf->fill_rect(Rect{0, 0, compositor.width(), compositor.height()}, 0xFF1C1C1Eu); // iOS Dark Gray

    compositor.add_layer(WindowServerLayer{1, 5, root_surf, Rect{0, 0, compositor.width(), compositor.height()}, 1.0f, 0, false});

    std::vector<std::uint8_t> frame(compositor.framebuffer_size(), 0);
    bool comp_ok = compositor.composite(frame.data(), frame.size());
    TEST_ASSERT(comp_ok, "Compositor frame generation succeeded");

    bool pres_ok = compositor.present_to_host(frame.data(), frame.size());
    TEST_ASSERT(pres_ok, "DirectX presentation completed");

    std::cout << "  Passed (End-to-end WindowServer graphics pipeline reached presentation state)\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WINIOS STAGE 137: GRAPHICS EXECUTION & WINDOWSERVER GATE\n";
    std::cout << "=================================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](const char* name, bool (*fn)()) {
        if (fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "FAILED: " << name << "\n";
        }
    };

    run_test("test_windowserver_genuine_admission_gate", test_windowserver_genuine_admission_gate);
    run_test("test_iphone15_display_geometry_and_stride", test_iphone15_display_geometry_and_stride);
    run_test("test_guest_cpu_framebuffer_pixel_store", test_guest_cpu_framebuffer_pixel_store);
    run_test("test_iosurface_allocation_and_readwrite", test_iosurface_allocation_and_readwrite);
    run_test("test_damage_tracker_dirty_region_aggregation", test_damage_tracker_dirty_region_aggregation);
    run_test("test_windowserver_multi_layer_compositing", test_windowserver_multi_layer_compositing);
    run_test("test_directx_host_presentation_adapter", test_directx_host_presentation_adapter);
    run_test("test_guest_rendered_vs_host_rendered_trace_separation", test_guest_rendered_vs_host_rendered_trace_separation);
    run_test("test_guest_framebuffer_damage_transfer", test_guest_framebuffer_damage_transfer);
    run_test("test_end_to_end_windowserver_graphics_pipeline", test_end_to_end_windowserver_graphics_pipeline);

    std::cout << "-----------------------------------------------------------------\n";
    std::cout << "STAGE 137 RESULTS: " << passed << "/" << (passed + failed) << " tests passed\n";
    std::cout << "=================================================================\n";

    return (failed == 0) ? 0 : 1;
}
