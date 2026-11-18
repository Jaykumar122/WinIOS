#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "runtime/guest_app_contract.h"
#include "runtime/guest_runtime_lifecycle.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::runtime;

void test_contract_arg_block_validation() {
    auto b = GuestAppContract::CreateDefault(1, 0x100000000ULL, 0x7FFF0000ULL, 64 * 1024, 0x200000000ULL, 4 * 1024 * 1024);
    std::string err;
    TEST_ASSERT(GuestAppContract::ValidateArgBlock(b, &err));

    // Invalid version
    b.contract_version = 0x99999999;
    TEST_ASSERT(!GuestAppContract::ValidateArgBlock(b, &err));

    // Invalid PID
    b.contract_version = kGuestAppContractVersion;
    b.process_id = 0;
    TEST_ASSERT(!GuestAppContract::ValidateArgBlock(b, &err));

    // Misaligned stack
    b.process_id = 1;
    b.stack_base = 0x7FFF0001ULL;
    TEST_ASSERT(!GuestAppContract::ValidateArgBlock(b, &err));

    std::cout << "[PASS] test_contract_arg_block_validation\n";
}

void test_abi_negotiation() {
    uint32_t negotiated = 0;
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(kGuestAppContractVersion, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion);

    // Request higher version -> capped to max supported
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(0x00020000, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion);

    // Request older valid version
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(0x00010000, &negotiated));
    TEST_ASSERT(negotiated == 0x00010000);

    // Unsupported version below minimum
    TEST_ASSERT(!GuestAppContract::NegotiateAbiVersion(0x00000100, &negotiated));

    std::cout << "[PASS] test_abi_negotiation\n";
}

void test_runtime_lifecycle_normal() {
    GuestProcessConfig cfg{};
    cfg.process_id = 100;
    cfg.stack_base = 0x7FFF0000ULL; // 16-byte aligned

    GuestRuntimeLifecycle runtime(cfg);
    TEST_ASSERT(runtime.GetState() == RuntimeState::kUninitialized);

    std::string err;
    TEST_ASSERT(runtime.Initialize(&err));
    TEST_ASSERT(runtime.GetState() == RuntimeState::kStarting);

    TEST_ASSERT(runtime.Launch(&err));
    TEST_ASSERT(runtime.GetState() == RuntimeState::kRunning);

    runtime.Step(500);
    TEST_ASSERT(runtime.GetRetiredInstructions() == 500);

    runtime.RegisterAllocatedWindow(1);
    runtime.RegisterAllocatedTimer(5);
    runtime.RegisterOpenFileHandle(3);
    TEST_ASSERT(runtime.GetAllocatedWindows().size() == 1);
    TEST_ASSERT(runtime.GetAllocatedTimers().size() == 1);
    TEST_ASSERT(runtime.GetOpenFileHandles().size() == 1);

    TEST_ASSERT(runtime.Terminate(0, GuestTerminationReason::kNormal));
    TEST_ASSERT(runtime.GetState() == RuntimeState::kTerminated);
    TEST_ASSERT(runtime.GetExitDescriptor().exit_code == 0);
    TEST_ASSERT(runtime.GetExitDescriptor().retired_instructions == 500);

    // Teardown cleaned up resources
    TEST_ASSERT(runtime.GetAllocatedWindows().empty());
    TEST_ASSERT(runtime.GetAllocatedTimers().empty());
    TEST_ASSERT(runtime.GetOpenFileHandles().empty());

    std::cout << "[PASS] test_runtime_lifecycle_normal\n";
}

void test_runtime_lifecycle_fault_rollback() {
    GuestProcessConfig cfg{};
    cfg.process_id = 101;
    cfg.stack_base = 0x7FFF0005ULL; // Misaligned!

    GuestRuntimeLifecycle runtime(cfg);
    std::string err;
    TEST_ASSERT(!runtime.Initialize(&err));
    TEST_ASSERT(runtime.GetState() == RuntimeState::kFaulted);
    TEST_ASSERT(!err.empty());

    std::cout << "[PASS] test_runtime_lifecycle_fault_rollback\n";
}

int main() {
    test_contract_arg_block_validation();
    test_abi_negotiation();
    test_runtime_lifecycle_normal();
    test_runtime_lifecycle_fault_rollback();
    std::cout << "All Stage 217.2-217.4 contract and lifecycle tests passed.\n";
    return 0;
}
