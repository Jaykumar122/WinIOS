// ============================================================================
// Stage 214.4: Frame and Presentation Accounting Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>

#include "graphics/frame_accounting.h"

using namespace winios::graphics;

namespace {

int g_failures = 0;

#define ACCT_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ACCT_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [ACCT_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_accounting_counters() {
    std::cout << "[Stage 214.4] Verifying 8 Frame Accounting Counters...\n";
    auto& acct = FrameAccounting::instance();
    acct.reset();

    const auto& s0 = acct.stats();
    ACCT_CHECK(s0.guest_surface_presents == 0, "Counter 1 initial 0");
    ACCT_CHECK(s0.damage_triggered_compositor_passes == 0, "Counter 2 initial 0");
    ACCT_CHECK(s0.composed_frames == 0, "Counter 3 initial 0");
    ACCT_CHECK(s0.host_presentation_attempts == 0, "Counter 4 initial 0");
    ACCT_CHECK(s0.successful_host_frames == 0, "Counter 5 initial 0");
    ACCT_CHECK(s0.repeated_frames == 0, "Counter 6 initial 0");
    ACCT_CHECK(s0.dropped_frames == 0, "Counter 7 initial 0");
    ACCT_CHECK(s0.vsync_deadline_events == 0, "Counter 8 initial 0");

    // Simulate Campaign 213 13-step demonstration scenario
    // 5 guest surface presents triggering 5 compositor passes:
    for (int i = 0; i < 5; ++i) {
        acct.record_guest_surface_present();
        acct.record_damage_compositor_pass();
        acct.record_composed_frame();
        acct.record_host_presentation_attempt();
        acct.record_successful_host_frame();
        acct.record_vsync_deadline();
    }

    // Plus 2 host frames captured after window exit where no compositor pass occurred (repeated frames)
    for (int i = 0; i < 2; ++i) {
        acct.record_host_presentation_attempt();
        acct.record_successful_host_frame();
        acct.record_repeated_frame();
        acct.record_vsync_deadline();
    }

    // Plus 1 simulated dropped frame
    acct.record_dropped_frame();

    const auto& s1 = acct.stats();
    ACCT_CHECK(s1.guest_surface_presents == 5, "Guest surface presents == 5");
    ACCT_CHECK(s1.damage_triggered_compositor_passes == 5, "Damage passes == 5");
    ACCT_CHECK(s1.composed_frames == 5, "Composed frames == 5");
    ACCT_CHECK(s1.host_presentation_attempts == 7, "Host presentation attempts == 7");
    ACCT_CHECK(s1.successful_host_frames == 7, "Successful host frames == 7");
    ACCT_CHECK(s1.repeated_frames == 2, "Repeated frames == 2");
    ACCT_CHECK(s1.dropped_frames == 1, "Dropped frames == 1");
    ACCT_CHECK(s1.vsync_deadline_events == 7, "Vsync deadline events == 7");

    // Arithmetic identity verification
    ACCT_CHECK(s1.composed_frames + s1.repeated_frames == s1.successful_host_frames,
               "Identity confirmed: composed (5) + repeated (2) == presented (7)");

    std::string report = acct.format_report();
    ACCT_CHECK(!report.empty(), "Report generated");
    ACCT_CHECK(report.find("Guest Surface Presents:                  5") != std::string::npos, "Report contains presents");
    ACCT_CHECK(report.find("Repeated Frames (No Pixel Mutation):     2") != std::string::npos, "Report contains repeated");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.4: Frame Accounting Tests         \n";
    std::cout << "====================================================\n";

    test_accounting_counters();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL ACCOUNTING TESTS PASSED" : "ACCOUNTING FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
