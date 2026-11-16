#include <iostream>
#include <cstdint>
#include <string>

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

enum class KernelBootState : uint32_t {
    Bootstrapping = 0,
    PlatformExpertInitialized = 1,
    EarlyVMInitialized = 2,
    PhysicalBoundsRegistered = 3,
    PhysicalPageMetadataInitialized = 4,
    KernelVMInitialized = 5
};

class KernelInitStateMachine {
public:
    KernelInitStateMachine() = default;

    KernelBootState current_state() const noexcept { return state_; }

    bool transition_to(KernelBootState next, std::string* out_error = nullptr) {
        const uint32_t curr = static_cast<uint32_t>(state_);
        const uint32_t nxt  = static_cast<uint32_t>(next);

        if (nxt != curr + 1) {
            if (out_error) {
                *out_error = "Illegal state transition from " + std::to_string(curr) + " to " + std::to_string(nxt);
            }
            return false;
        }

        state_ = next;
        return true;
    }

private:
    KernelBootState state_{KernelBootState::Bootstrapping};
};

void test_kernel_init_state_machine() {
    std::cout << "[Test 1] Stage 209.19: Kernel Initialization State Machine Progression...\n";

    KernelInitStateMachine sm;
    std::string err;

    TEST_CHECK(sm.current_state() == KernelBootState::Bootstrapping, "Initial state is Bootstrapping");

    // 1. Advance to PlatformExpertInitialized
    TEST_CHECK(sm.transition_to(KernelBootState::PlatformExpertInitialized, &err), "Transitioned to PlatformExpertInitialized");

    // 2. Advance to EarlyVMInitialized
    TEST_CHECK(sm.transition_to(KernelBootState::EarlyVMInitialized, &err), "Transitioned to EarlyVMInitialized");

    // 3. Advance to PhysicalBoundsRegistered
    TEST_CHECK(sm.transition_to(KernelBootState::PhysicalBoundsRegistered, &err), "Transitioned to PhysicalBoundsRegistered");

    // 4. Advance to PhysicalPageMetadataInitialized
    TEST_CHECK(sm.transition_to(KernelBootState::PhysicalPageMetadataInitialized, &err), "Transitioned to PhysicalPageMetadataInitialized");

    // 5. Advance to KernelVMInitialized
    TEST_CHECK(sm.transition_to(KernelBootState::KernelVMInitialized, &err), "Transitioned to KernelVMInitialized");

    // 6. Test illegal transition (e.g. attempting to skip backward or jump forward)
    TEST_CHECK(!sm.transition_to(KernelBootState::Bootstrapping, &err), "Illegal backward transition rejected");
    TEST_CHECK(!err.empty(), "Error message returned for illegal transition");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.19 Kernel Init State Machine Tests ===\n";
    test_kernel_init_state_machine();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.19: All kernel init state machine tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.19: " << g_failures << " tests failed.\n";
        return 1;
    }
}
