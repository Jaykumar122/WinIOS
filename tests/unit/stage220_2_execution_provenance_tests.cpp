#include <iostream>
#include <vector>
#include <cstdint>
#include <cassert>

#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

static void test_initial_state() {
    std::cout << "[Test 1] ExecutionBoundaryEnforcer initial state & reset...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kUnknown);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);
    TEST_ASSERT(enforcer.GetTotalSchedulerOperations() == 0);
    TEST_ASSERT(enforcer.GetTotalSyscallServices() == 0);
    TEST_ASSERT(enforcer.GetCurrentSequenceNumber() == 0);
    TEST_ASSERT(enforcer.GetRecentEvents().empty());
}

static void test_genuine_guest_retirement() {
    std::cout << "[Test 2] Genuine guest instruction retirement tracking with PID/TID...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        enforcer.SetActiveContext(100, 1001, 0x100004000ULL);

        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(42));
        TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 42);
        TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);

        TEST_ASSERT(enforcer.RecordGuestInstructionRetirementExplicit(8, 100, 1001, 0x1000040A8ULL));
        TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 50);
    }

    auto events = enforcer.GetRecentEvents();
    TEST_ASSERT(events.size() == 2);
    TEST_ASSERT(events[0].sequence_number == 1);
    TEST_ASSERT(events[0].origin == ExecutionOrigin::kGuestCpuRetired);
    TEST_ASSERT(events[0].pid == 100);
    TEST_ASSERT(events[0].tid == 1001);
    TEST_ASSERT(events[0].instruction_count == 42);
    TEST_ASSERT(events[0].guest_pc == 0x100004000ULL);

    TEST_ASSERT(events[1].sequence_number == 2);
    TEST_ASSERT(events[1].origin == ExecutionOrigin::kGuestCpuRetired);
    TEST_ASSERT(events[1].instruction_count == 8);
    TEST_ASSERT(events[1].guest_pc == 0x1000040A8ULL);
}

static void test_adversarial_rejection() {
    std::cout << "[Test 3] Adversarial rejection of non-guest execution origins...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    const std::vector<ExecutionOrigin> adversarial_origins = {
        ExecutionOrigin::kUnknown,
        ExecutionOrigin::kHostSimulation,
        ExecutionOrigin::kKernelSyscallService,
        ExecutionOrigin::kHostPresentation,
        ExecutionOrigin::kHostSchedulerOperation,
        ExecutionOrigin::kTestInstrumentation
    };

    uint64_t expected_rejected = 0;
    for (auto origin : adversarial_origins) {
        ExecutionOriginScope scope(origin);
        enforcer.SetActiveContext(101, 2002, 0x2000ULL);

        bool result = enforcer.RecordGuestInstructionRetirement(10);
        TEST_ASSERT(!result);
        expected_rejected++;
        TEST_ASSERT(enforcer.GetRejectedIncrements() == expected_rejected);
        TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    }

    TEST_ASSERT(enforcer.GetRejectedIncrements() == adversarial_origins.size());
}

static void test_scheduler_and_syscall_accounting() {
    std::cout << "[Test 4] Scheduler and syscall event accounting & provenance sequence...\n";
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    enforcer.RecordSchedulerOperation(100, 1001);
    enforcer.RecordSyscallService(100, 1001);
    enforcer.RecordSchedulerOperation(101, 2001);

    TEST_ASSERT(enforcer.GetTotalSchedulerOperations() == 2);
    TEST_ASSERT(enforcer.GetTotalSyscallServices() == 1);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0); // Must NOT be counted as instructions

    auto events = enforcer.GetRecentEvents();
    TEST_ASSERT(events.size() == 3);
    TEST_ASSERT(events[0].sequence_number == 1);
    TEST_ASSERT(events[0].origin == ExecutionOrigin::kHostSchedulerOperation);
    TEST_ASSERT(events[1].sequence_number == 2);
    TEST_ASSERT(events[1].origin == ExecutionOrigin::kKernelSyscallService);
    TEST_ASSERT(events[2].sequence_number == 3);
    TEST_ASSERT(events[2].origin == ExecutionOrigin::kHostSchedulerOperation);
}

int main() {
    std::cout << "=== WinIOS Stage 220.2 Execution Provenance Tests ===\n";
    test_initial_state();
    test_genuine_guest_retirement();
    test_adversarial_rejection();
    test_scheduler_and_syscall_accounting();
    std::cout << "[PASS] All Stage 220.2 Execution Provenance tests passed.\n";
    return 0;
}
