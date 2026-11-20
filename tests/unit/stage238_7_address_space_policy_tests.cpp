// ============================================================================
// Stage 238.7: Address-Space Policy & Memory Validator Audit Tests
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "tests/support/test_assert.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "memory/guest_memory_validator.h"

using namespace winios::memory;

namespace {

void test_policy_instantiation_and_limits() {
    std::cout << "[Test 1] Address Space Policy Object & Limits...\n";
    Memory mem{0x2000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    ppm.initialize(false);

    // 1. StandardGeneral Policy (Default)
    const std::uint64_t root1 = ppm.allocate_page();
    UserAddressSpace uas_std(mem, ppm, root1, "general_uas", AddressSpacePolicy::StandardGeneral);
    TEST_ASSERT(uas_std.policy() == AddressSpacePolicy::StandardGeneral);
    TEST_ASSERT(uas_std.user_space_limit() == kUserSpaceLimitVa, "General space limit must be 32 GiB");
    TEST_ASSERT(uas_std.user_space_limit() == 0x0000000800000000ULL);

    // 2. StrictSandbox Policy Explicit
    const std::uint64_t root2 = ppm.allocate_page();
    UserAddressSpace uas_sand(mem, ppm, root2, "my_sandbox", AddressSpacePolicy::StrictSandbox);
    TEST_ASSERT(uas_sand.policy() == AddressSpacePolicy::StrictSandbox);
    TEST_ASSERT(uas_sand.user_space_limit() == kSandboxSpaceLimitVa, "Strict sandbox limit must be 2 GiB");
    TEST_ASSERT(uas_sand.user_space_limit() == 0x0000000080000000ULL);

    // 3. Backward Compatibility via "sec_uas" name
    const std::uint64_t root3 = ppm.allocate_page();
    UserAddressSpace uas_legacy(mem, ppm, root3, "sec_uas");
    TEST_ASSERT(uas_legacy.policy() == AddressSpacePolicy::StrictSandbox, "Legacy 'sec_uas' name must map to StrictSandbox");
    TEST_ASSERT(uas_legacy.user_space_limit() == 0x0000000080000000ULL);
    std::cout << "  -> Passed policy instantiation and limits\n";
}

void test_validator_enforcement_across_policies() {
    std::cout << "[Test 2] GuestMemoryValidator Policy Enforcement...\n";
    Memory mem{0x2000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    ppm.initialize(false);

    const std::uint64_t root_std = ppm.allocate_page();
    UserAddressSpace uas_std(mem, ppm, root_std, "general_uas", AddressSpacePolicy::StandardGeneral);

    const std::uint64_t root_sand = ppm.allocate_page();
    UserAddressSpace uas_sand(mem, ppm, root_sand, "sandbox_uas", AddressSpacePolicy::StrictSandbox);

    // Address 0x80000000ULL (2 GiB)
    const std::uint64_t target_va = 0x80000000ULL;

    // Under StandardGeneral (32 GiB limit): 2 GiB is inside canonical space -> UnmappedPage
    auto res_std = GuestMemoryValidator::validate_buffer(uas_std, target_va, 64, false);
    TEST_ASSERT(res_std == ValidationResult::UnmappedPage, "2 GiB is within 32 GiB bounds, so should report UnmappedPage");

    // Under StrictSandbox (2 GiB limit): 2 GiB is at or beyond boundary -> OutOfBounds
    auto res_sand = GuestMemoryValidator::validate_buffer(uas_sand, target_va, 64, false);
    TEST_ASSERT(res_sand == ValidationResult::OutOfBounds, "2 GiB must be OutOfBounds under StrictSandbox");

    // Copy string test at 2 GiB
    std::string out_str;
    auto str_res_sand = GuestMemoryValidator::copy_string(uas_sand, target_va, 64, out_str);
    TEST_ASSERT(str_res_sand == ValidationResult::OutOfBounds, "copy_string must report OutOfBounds under StrictSandbox");

    std::cout << "  -> Passed validator enforcement across policies\n";
}

void test_bounds_and_overflow_vectors() {
    std::cout << "[Test 3] Bounds & Integer Overflow Vectors...\n";
    Memory mem{0x2000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    ppm.initialize(false);

    const std::uint64_t root = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root, "uas", AddressSpacePolicy::StrictSandbox);

    // 1. Integer overflow (va + size < va)
    auto res_ov = GuestMemoryValidator::validate_buffer(uas, 0xFFFFFFFFFFFFFFFFULL, 16, false);
    TEST_ASSERT(res_ov == ValidationResult::IntegerOverflow);

    // 2. Zero length forbidden by default
    auto res_zero = GuestMemoryValidator::validate_buffer(uas, 0x10000, 0, false, 1, false);
    TEST_ASSERT(res_zero == ValidationResult::ZeroLengthForbidden);

    // 3. Zero length allowed when flag is set
    auto res_zero_ok = GuestMemoryValidator::validate_buffer(uas, 0x10000, 0, false, 1, true);
    TEST_ASSERT(res_zero_ok == ValidationResult::Success);

    // 4. Misaligned pointer
    auto res_align = GuestMemoryValidator::validate_buffer(uas, 0x10003, 16, false, 8);
    TEST_ASSERT(res_align == ValidationResult::MisalignedPointer);

    std::cout << "  -> Passed bounds and overflow vectors\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.7: Address-Space Policy & Validator Suite    \n";
    std::cout << "========================================================\n";

    test_policy_instantiation_and_limits();
    test_validator_enforcement_across_policies();
    test_bounds_and_overflow_vectors();

    std::cout << "\nALL STAGE 238.7 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
