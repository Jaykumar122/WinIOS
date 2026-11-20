#include "tests/support/test_assert.h"
#include "runtime/runtime_lifecycle_state_machine.h"

#include <iostream>
#include <vector>
#include <thread>

using namespace winios::runtime;

namespace {

void test_normal_forward_lifecycle() {
    std::cout << "[Test 1] Normal Forward Lifecycle Sequence...\n";
    RuntimeLifecycleStateMachine sm(100, 1, "com.winios.app1");

    TEST_ASSERT(sm.state() == AppLifecycleState::Created);
    TEST_ASSERT(!sm.is_terminal());

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Admitted, "Admitted via manifest"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Admitted);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Loading, "Loading Mach-O sections"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Loading);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Initialized, "Runtime initialized"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Initialized);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Running, "Brought to foreground"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Running);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Suspended, "Background suspended"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Suspended);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Running, "Resumed to foreground"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Running);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Terminating, "User requested quit"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Terminating);

    TEST_ASSERT(sm.transition_to(AppLifecycleState::Terminated, "Resources freed"));
    TEST_ASSERT(sm.state() == AppLifecycleState::Terminated);
    TEST_ASSERT(sm.is_terminal());

    // Verify history has recorded 8 transitions
    auto hist = sm.history();
    TEST_ASSERT(hist.size() == 8);

    std::cout << "  -> Passed normal forward lifecycle sequence\n";
}

void test_invalid_transition_rejection() {
    std::cout << "[Test 2] Invalid Transition Rejection...\n";
    RuntimeLifecycleStateMachine sm(101, 1, "com.winios.app2");

    // Cannot jump from Created directly to Running
    TEST_ASSERT(!sm.transition_to(AppLifecycleState::Running));
    TEST_ASSERT(sm.state() == AppLifecycleState::Created);

    // Cannot jump from Created directly to Suspended
    TEST_ASSERT(!sm.transition_to(AppLifecycleState::Suspended));
    TEST_ASSERT(sm.state() == AppLifecycleState::Created);

    // Transition to Admitted
    TEST_ASSERT(sm.transition_to(AppLifecycleState::Admitted));

    // Cannot transition from Admitted directly to Terminated
    TEST_ASSERT(!sm.transition_to(AppLifecycleState::Terminated));
    TEST_ASSERT(sm.state() == AppLifecycleState::Admitted);

    // Same state transition must be rejected
    TEST_ASSERT(!sm.transition_to(AppLifecycleState::Admitted));

    std::cout << "  -> Passed invalid transition rejection\n";
}

void test_terminal_states_immutable() {
    std::cout << "[Test 3] Terminal States Immutability...\n";
    RuntimeLifecycleStateMachine sm1(102, 1, "com.winios.term");
    TEST_ASSERT(sm1.transition_to(AppLifecycleState::Failed, "Verification failure"));
    TEST_ASSERT(sm1.state() == AppLifecycleState::Failed);
    TEST_ASSERT(sm1.is_terminal());

    // Cannot transition out of Failed
    TEST_ASSERT(!sm1.transition_to(AppLifecycleState::Created));
    TEST_ASSERT(!sm1.transition_to(AppLifecycleState::Running));
    TEST_ASSERT(!sm1.transition_to(AppLifecycleState::Terminated));
    TEST_ASSERT(sm1.state() == AppLifecycleState::Failed);

    RuntimeLifecycleStateMachine sm2(103, 1, "com.winios.term2");
    sm2.transition_to(AppLifecycleState::Admitted);
    sm2.transition_to(AppLifecycleState::Loading);
    sm2.transition_to(AppLifecycleState::Initialized);
    sm2.transition_to(AppLifecycleState::Terminating);
    sm2.transition_to(AppLifecycleState::Terminated);
    TEST_ASSERT(sm2.state() == AppLifecycleState::Terminated);
    TEST_ASSERT(sm2.is_terminal());

    // Cannot transition out of Terminated
    TEST_ASSERT(!sm2.transition_to(AppLifecycleState::Running));
    TEST_ASSERT(!sm2.transition_to(AppLifecycleState::Created));
    TEST_ASSERT(sm2.state() == AppLifecycleState::Terminated);

    std::cout << "  -> Passed terminal states immutability\n";
}

void test_double_cleanup_prevention() {
    std::cout << "[Test 4] Double Cleanup Prevention...\n";
    RuntimeLifecycleStateMachine sm(104, 1, "com.winios.cleanup");

    TEST_ASSERT(!sm.is_cleanup_claimed());

    // First cleanup claim must succeed
    TEST_ASSERT(sm.begin_cleanup());
    TEST_ASSERT(sm.is_cleanup_claimed());

    // Second cleanup claim must fail (prevents double-cleanup)
    TEST_ASSERT(!sm.begin_cleanup());
    TEST_ASSERT(sm.is_cleanup_claimed());

    // Concurrency test on cleanup claim
    RuntimeLifecycleStateMachine sm_concurrent(105, 1, "com.winios.concurrent");
    int claimed_count = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&]() {
            if (sm_concurrent.begin_cleanup()) {
                ++claimed_count;
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }

    TEST_ASSERT(claimed_count == 1, "Exactly one thread must claim cleanup");

    std::cout << "  -> Passed double cleanup prevention\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 239.3: Runtime Lifecycle State Machine Tests     \n";
    std::cout << "========================================================\n";

    test_normal_forward_lifecycle();
    test_invalid_transition_rejection();
    test_terminal_states_immutable();
    test_double_cleanup_prevention();

    std::cout << "\nALL STAGE 239.3 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
