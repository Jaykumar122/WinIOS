// ============================================================================
// Stage 214.16: Application Lifecycle Manager Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <vector>

#include "kernel/app_lifecycle_manager.h"

using namespace winios::kernel;

namespace {

int g_failures = 0;

#define LIFE_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[LIFE_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [LIFE_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_lifecycle_state_machine() {
    std::cout << "[Stage 214.16] Verifying 8-State Application Lifecycle Machine...\n";

    AppLifecycleManager mgr;
    std::vector<std::pair<AppState, AppState>> observed;

    mgr.set_state_callback([&](std::uint32_t /*pid*/, AppState old_s, AppState new_s) {
        observed.emplace_back(old_s, new_s);
    });

    const std::uint32_t pid = 42;

    // 1. Register app
    LIFE_CHECK(mgr.register_app(pid, "com.winios.testapp"), "Registered app in Created state");
    LIFE_CHECK(mgr.get_app_state(pid) == AppState::Created, "State is Created");

    // 2. Legal sequence: Created -> Starting -> Foreground
    LIFE_CHECK(mgr.transition_to(pid, AppState::Starting), "Transition to Starting");
    LIFE_CHECK(mgr.transition_to(pid, AppState::Foreground), "Transition to Foreground");
    LIFE_CHECK(mgr.foreground_pid() == pid, "Foreground PID is 42");

    // 3. Illegal transition: Foreground -> Created (REJECTED)
    LIFE_CHECK(!mgr.transition_to(pid, AppState::Created), "Illegal Foreground -> Created rejected");

    // 4. Backgrounding & Suspension: Foreground -> Background -> Suspended
    LIFE_CHECK(mgr.transition_to(pid, AppState::Background), "Transition to Background");
    LIFE_CHECK(mgr.foreground_pid() == 0, "Foreground PID cleared");
    LIFE_CHECK(mgr.transition_to(pid, AppState::Suspended), "Transition to Suspended");

    // 5. Illegal transition: Suspended -> Starting (REJECTED)
    LIFE_CHECK(!mgr.transition_to(pid, AppState::Starting), "Illegal Suspended -> Starting rejected");

    // 6. Resuming: Suspended -> Resuming -> Foreground
    LIFE_CHECK(mgr.transition_to(pid, AppState::Resuming), "Transition to Resuming");
    LIFE_CHECK(mgr.transition_to(pid, AppState::Foreground), "Transition to Foreground");
    LIFE_CHECK(mgr.foreground_pid() == pid, "Foreground PID restored");

    // 7. Termination: Foreground -> Terminating -> Terminated
    LIFE_CHECK(mgr.transition_to(pid, AppState::Terminating), "Transition to Terminating");
    LIFE_CHECK(mgr.transition_to(pid, AppState::Terminated), "Transition to Terminated");
    LIFE_CHECK(mgr.get_app_state(pid) == AppState::Terminated, "Final state is Terminated");

    // 8. Illegal transition from terminal state: Terminated -> Foreground (REJECTED)
    LIFE_CHECK(!mgr.transition_to(pid, AppState::Foreground), "Illegal Terminated -> Foreground rejected");

    // Verify all valid transitions were recorded in callback
    LIFE_CHECK(observed.size() == 8, "Recorded 8 valid state transitions");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.16: Lifecycle Tests               \n";
    std::cout << "====================================================\n";

    test_lifecycle_state_machine();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL LIFECYCLE TESTS PASSED" : "LIFECYCLE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
