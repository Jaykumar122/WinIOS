#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/guest_app_contract.h"
#include "kernel/user_stack.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::kernel;

void test_abi_v1_5_contract_negotiation() {
    std::cout << "[Test 1] ABI v1.5 Contract Negotiation...\n";
    uint32_t negotiated = 0;
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(kGuestAppContractVersion_1_5, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion_1_5);

    // Requesting higher version caps to max supported
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(0x00020000, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion_1_5);

    // Older versions remain supported
    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(kGuestAppContractVersion_1_2, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion_1_2);

    TEST_ASSERT(GuestAppContract::NegotiateAbiVersion(kGuestAppContractVersion_1_0, &negotiated));
    TEST_ASSERT(negotiated == kGuestAppContractVersion_1_0);

    // Unsupported ancient version
    TEST_ASSERT(!GuestAppContract::NegotiateAbiVersion(0x00000001, &negotiated));
    std::cout << "[PASS] ABI v1.5 negotiation passed.\n";
}

void test_arg_block_geometry_and_alignment() {
    std::cout << "[Test 2] GuestAppArgBlock Geometry & Validation...\n";
    static_assert(sizeof(GuestAppArgBlock) == 96, "GuestAppArgBlock must be exactly 96 bytes");

    auto b = GuestAppContract::CreateDefault(10, 0x100000000ULL, 0x7FFF0000ULL, 0x10000ULL, 0x200000000ULL, 0x40000ULL);
    std::string err;
    TEST_ASSERT(GuestAppContract::ValidateArgBlock(b, &err));
    TEST_ASSERT(b.contract_version == kGuestAppContractVersion_1_5);
    TEST_ASSERT(b.process_id == 10);

    // Stack alignment check
    TEST_ASSERT(GuestAppContract::ValidateStackAlignment(b.stack_base));
    TEST_ASSERT(!GuestAppContract::ValidateStackAlignment(b.stack_base + 1));
    TEST_ASSERT(!GuestAppContract::ValidateStackAlignment(b.stack_base + 8));

    // Invalid bounds
    b.heap_limit = b.heap_base;
    TEST_ASSERT(!GuestAppContract::ValidateArgBlock(b, &err));

    std::cout << "[PASS] ArgBlock geometry and alignment validated.\n";
}

void test_initial_stack_formatting() {
    std::cout << "[Test 3] UserStackBuilder Initial Stack Formatting...\n";
    Memory mem(0x800000);
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "test_stack_uas");

    UserStackConfig cfg;
    cfg.stack_top_va = 0x70000ULL;
    cfg.stack_size = 0x10000;
    cfg.argv = { "runtime_explorer", "--verbose", "--theme=dark" };
    cfg.envp = { "PATH=/apps/bin", "USER=guest_user", "WINIOS_ABI=1.5" };

    auto res = UserStackBuilder::build_stack(uas, ppm, mem, cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.argc == 3);
    TEST_ASSERT((res.final_sp_el0 & 0xF) == 0); // 16-byte aligned

    // Verify argc on stack
    uint64_t stack_argc = 0;
    TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(res.final_sp_el0), &stack_argc, sizeof(stack_argc)));
    TEST_ASSERT(stack_argc == 3);

    std::cout << "[PASS] User stack successfully formatted.\n";
}

int main() {
    std::cout << "=== WinIOS Stage 221.2: Guest Application Startup Contract Tests ===\n";
    test_abi_v1_5_contract_negotiation();
    test_arg_block_geometry_and_alignment();
    test_initial_stack_formatting();
    std::cout << "[ALL PASS] Stage 221.2 tests passed successfully.\n";
    return 0;
}
