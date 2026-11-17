#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "memory/guest_memory_validator.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::memory;

void test_null_and_bounds() {
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_bounds");

    // 1. Null pointer
    auto res_null = GuestMemoryValidator::validate_buffer(uas, 0, 16, false);
    assert(res_null == ValidationResult::NullPointer);

    // 2. Zero length forbidden by default
    auto res_zero = GuestMemoryValidator::validate_buffer(uas, 0x10000, 0, false, 1, false);
    assert(res_zero == ValidationResult::ZeroLengthForbidden);

    // 3. Zero length allowed if flag set
    auto res_zero_ok = GuestMemoryValidator::validate_buffer(uas, 0x10000, 0, false, 1, true);
    assert(res_zero_ok == ValidationResult::Success);

    // 4. Integer overflow
    auto res_ovf = GuestMemoryValidator::validate_buffer(uas, 0xFFFFFFFFFFFFFFF0ULL, 32, false);
    assert(res_ovf == ValidationResult::IntegerOverflow);

    // 5. Out of bounds (above kUserSpaceLimitVa = 0x80000000)
    auto res_oob = GuestMemoryValidator::validate_buffer(uas, kUserSpaceLimitVa - 10, 20, false);
    assert(res_oob == ValidationResult::OutOfBounds);

    std::cout << "[PASS] test_null_and_bounds passed\n";
}

void test_alignment() {
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_align");

    // Map a valid page at 0x10000 (64KB)
    const std::uint64_t pa = ppm.allocate_page();
    assert(pa != 0);
    bool map_ok = uas.map_user_page(0x10000, pa, PagePermissions::UserReadWrite);
    assert(map_ok);

    // 4-byte alignment check
    auto res_aligned4 = GuestMemoryValidator::validate_buffer(uas, 0x10000, 16, false, 4);
    assert(res_aligned4 == ValidationResult::Success);

    auto res_misaligned4 = GuestMemoryValidator::validate_buffer(uas, 0x10001, 16, false, 4);
    assert(res_misaligned4 == ValidationResult::MisalignedPointer);

    // 8-byte alignment check
    auto res_aligned8 = GuestMemoryValidator::validate_buffer(uas, 0x10008, 16, false, 8);
    assert(res_aligned8 == ValidationResult::Success);

    auto res_misaligned8 = GuestMemoryValidator::validate_buffer(uas, 0x10004, 16, false, 8);
    assert(res_misaligned8 == ValidationResult::MisalignedPointer);

    std::cout << "[PASS] test_alignment passed\n";
}

void test_permissions_and_unmapped() {
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_perms");

    // Address in unmapped range
    auto res_unmapped = GuestMemoryValidator::validate_buffer(uas, 0x20000, 64, false);
    assert(res_unmapped == ValidationResult::UnmappedPage);

    // Map 0x20000 as UserRead
    const std::uint64_t pa_ro = ppm.allocate_page();
    assert(pa_ro != 0);
    assert(uas.map_user_page(0x20000, pa_ro, PagePermissions::UserRead));

    // Valid for read
    auto res_ro_read = GuestMemoryValidator::validate_buffer(uas, 0x20000, 64, false);
    assert(res_ro_read == ValidationResult::Success);

    // Fails for write
    auto res_ro_write = GuestMemoryValidator::validate_buffer(uas, 0x20000, 64, true);
    assert(res_ro_write == ValidationResult::PermissionDenied);

    // Map 0x24000 as UserReadWrite (adjacent 16KB page)
    const std::uint64_t pa_rw = ppm.allocate_page();
    assert(pa_rw != 0);
    assert(uas.map_user_page(0x24000, pa_rw, PagePermissions::UserReadWrite));

    // Spanning across boundary from 0x20000 (RO) into 0x24000 (RW):
    // Span reading across boundary should succeed
    const std::uint64_t span_va = 0x20000 + kPhysicalPageSize16KB - 32;
    auto res_span_read = GuestMemoryValidator::validate_buffer(uas, span_va, 64, false);
    assert(res_span_read == ValidationResult::Success);

    // Span writing across boundary should fail (first page is RO)
    auto res_span_write = GuestMemoryValidator::validate_buffer(uas, span_va, 64, true);
    assert(res_span_write == ValidationResult::PermissionDenied);

    // Spanning into unmapped third page (0x28000)
    const std::uint64_t span2_va = 0x24000 + kPhysicalPageSize16KB - 32;
    auto res_span_unmapped = GuestMemoryValidator::validate_buffer(uas, span2_va, 64, false);
    assert(res_span_unmapped == ValidationResult::UnmappedPage);

    std::cout << "[PASS] test_permissions_and_unmapped passed\n";
}

void test_copyin_copyout_data() {
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_copy");

    // Map two contiguous 16KB pages: 0x30000 and 0x34000
    const std::uint64_t pa1 = ppm.allocate_page();
    const std::uint64_t pa2 = ppm.allocate_page();
    assert(uas.map_user_page(0x30000, pa1, PagePermissions::UserReadWrite));
    assert(uas.map_user_page(0x34000, pa2, PagePermissions::UserReadWrite));

    // Prepare host data that straddles the 16KB page boundary
    const std::uint64_t straddle_va = 0x30000 + kPhysicalPageSize16KB - 16;
    const std::size_t test_len = 32;
    std::vector<std::uint8_t> src_data(test_len);
    for (std::size_t i = 0; i < test_len; ++i) {
        src_data[i] = static_cast<std::uint8_t>(0xA0 + i);
    }

    // Copy to user space (host -> guest)
    auto res_out = GuestMemoryValidator::copyout(uas, src_data.data(), straddle_va, test_len, 4);
    assert(res_out == ValidationResult::Success);

    // Copy from user space (guest -> host)
    std::vector<std::uint8_t> readback(test_len, 0);
    auto res_in = GuestMemoryValidator::copyin(uas, straddle_va, readback.data(), test_len, 4);
    assert(res_in == ValidationResult::Success);

    // Verify bit-for-bit identity
    assert(std::memcmp(src_data.data(), readback.data(), test_len) == 0);

    std::cout << "[PASS] test_copyin_copyout_data passed\n";
}

void test_copy_string() {
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "test_string");

    const std::uint64_t pa = ppm.allocate_page();
    assert(uas.map_user_page(0x40000, pa, PagePermissions::UserReadWrite));

    const std::string original = "WinIOS Platform Hardening 215";
    auto res_out = GuestMemoryValidator::copyout(uas, original.c_str(), 0x40000, original.size() + 1);
    assert(res_out == ValidationResult::Success);

    std::string read_str;
    auto res_str = GuestMemoryValidator::copy_string(uas, 0x40000, 128, read_str);
    assert(res_str == ValidationResult::Success);
    assert(read_str == original);

    // Null pointer string
    std::string null_str;
    auto res_null = GuestMemoryValidator::copy_string(uas, 0, 128, null_str);
    assert(res_null == ValidationResult::NullPointer);

    // Unmapped string
    std::string unmapped_str;
    auto res_unm = GuestMemoryValidator::copy_string(uas, 0x50000, 128, unmapped_str);
    assert(res_unm == ValidationResult::UnmappedPage);

    std::cout << "[PASS] test_copy_string passed\n";
}

int main() {
    std::cout << "Starting Stage 215.4 GuestMemoryValidator Tests...\n";
    test_null_and_bounds();
    test_alignment();
    test_permissions_and_unmapped();
    test_copyin_copyout_data();
    test_copy_string();
    std::cout << "All Stage 215.4 GuestMemoryValidator tests PASSED!\n";
    return 0;
}
