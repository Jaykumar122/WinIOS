#include <iostream>
#include <cstdint>
#include <cstdlib>

#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

void test_boundary_enforcer_basics() {
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kUnknown);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);

    // Host simulation attempts to increment guest instruction counter -> must fail!
    enforcer.SetCurrentOrigin(ExecutionOrigin::kHostSimulation);
    TEST_ASSERT(!enforcer.RecordGuestInstructionRetirement(100));
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 1);

    // Kernel syscall service attempts to increment guest counter -> must fail!
    enforcer.SetCurrentOrigin(ExecutionOrigin::kKernelSyscallService);
    TEST_ASSERT(!enforcer.RecordGuestInstructionRetirement(50));
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 2);

    // Host presentation attempts to increment -> must fail!
    enforcer.SetCurrentOrigin(ExecutionOrigin::kHostPresentation);
    TEST_ASSERT(!enforcer.RecordGuestInstructionRetirement(25));
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 0);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 3);

    // Genuine Guest CPU execution -> must succeed!
    enforcer.SetCurrentOrigin(ExecutionOrigin::kGuestCpuRetired);
    TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(1));
    TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(10));
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 11);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 3);

    std::cout << "[PASS] test_boundary_enforcer_basics\n";
}

void test_boundary_raii_scope() {
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    {
        ExecutionOriginScope scope1(ExecutionOrigin::kGuestCpuRetired);
        TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kGuestCpuRetired);
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(42));

        {
            ExecutionOriginScope scope2(ExecutionOrigin::kKernelSyscallService);
            TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kKernelSyscallService);
            TEST_ASSERT(!enforcer.RecordGuestInstructionRetirement(1));
        }

        TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kGuestCpuRetired);
        TEST_ASSERT(enforcer.RecordGuestInstructionRetirement(8));
    }

    TEST_ASSERT(enforcer.GetCurrentOrigin() == ExecutionOrigin::kUnknown);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == 50);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 1);

    std::cout << "[PASS] test_boundary_raii_scope\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.2 Execution Boundary Tests ===\n";
    test_boundary_enforcer_basics();
    test_boundary_raii_scope();
    std::cout << "[ALL PASS] Stage 218.2 Execution Boundary verification succeeded.\n";
    return 0;
}
