#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <cstdlib>
#include <cstddef>
#include <cstring>

#include "memory/guest_memory_validator.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "kernel/user_syscall_dispatcher.h"
#include "winios_sdk/winios_sdk.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;

// Compile-Time ABI Conformance Static Assertions (Freestanding ARM64 Mach-O EL0 ABI)
namespace abi_checks {
    // IPC structures (guest-sdk/include/winios/ipc/ipc.h)
    static_assert(sizeof(winios_ipc_msg_desc_t) == 24, "winios_ipc_msg_desc_t must be 24 bytes");
    static_assert(offsetof(winios_ipc_msg_desc_t, sender_pid) == 0, "sender_pid offset must be 0");
    static_assert(offsetof(winios_ipc_msg_desc_t, target_port) == 4, "target_port offset must be 4");
    static_assert(offsetof(winios_ipc_msg_desc_t, data_ptr) == 8, "data_ptr offset must be 8");
    static_assert(offsetof(winios_ipc_msg_desc_t, data_size) == 16, "data_size offset must be 16");
    static_assert(offsetof(winios_ipc_msg_desc_t, message_id) == 20, "message_id offset must be 20");

    static_assert(sizeof(winios_ipc_msg_v2_t) == 32, "winios_ipc_msg_v2_t must be 32 bytes");
    static_assert(offsetof(winios_ipc_msg_v2_t, target_port) == 0, "target_port offset must be 0");
    static_assert(offsetof(winios_ipc_msg_v2_t, data_ptr) == 8, "data_ptr offset must be 8");
    static_assert(offsetof(winios_ipc_msg_v2_t, data_size) == 16, "data_size offset must be 16");
    static_assert(offsetof(winios_ipc_msg_v2_t, message_id) == 20, "message_id offset must be 20");
    static_assert(offsetof(winios_ipc_msg_v2_t, sender_pid) == 24, "sender_pid offset must be 24");
    static_assert(offsetof(winios_ipc_msg_v2_t, reserved) == 28, "reserved offset must be 28");

    static_assert(sizeof(winios_msg_header_t) == 24, "winios_msg_header_t must be 24 bytes");
    static_assert(offsetof(winios_msg_header_t, msg_size) == 0, "msg_size offset must be 0");
    static_assert(offsetof(winios_msg_header_t, msg_id) == 4, "msg_id offset must be 4");
    static_assert(offsetof(winios_msg_header_t, src_port) == 8, "src_port offset must be 8");
    static_assert(offsetof(winios_msg_header_t, dst_port) == 12, "dst_port offset must be 12");
    static_assert(offsetof(winios_msg_header_t, flags) == 16, "flags offset must be 16");
    static_assert(offsetof(winios_msg_header_t, reserved) == 20, "reserved offset must be 20");

    // System Settings & Info structures
    static_assert(sizeof(winios_settings_snapshot_t) == 48, "winios_settings_snapshot_t must be 48 bytes");
    static_assert(sizeof(winios_task_entry_t) == 120, "winios_task_entry_t must be 120 bytes");
    static_assert(sizeof(winios_accessibility_info_t) == 32, "winios_accessibility_info_t must be 32 bytes");
    static_assert(sizeof(winios_diag_event_record_t) == 48, "winios_diag_event_record_t must be 48 bytes");
    static_assert(sizeof(winios_diag_query_req_t) == 32, "winios_diag_query_req_t must be 32 bytes");
    static_assert(sizeof(winios_app_info_t) == 128, "winios_app_info_t must be 128 bytes");
    static_assert(sizeof(winios_app_list_req_t) == 24, "winios_app_list_req_t must be 24 bytes");
} // namespace abi_checks

struct MemoryFixture {
    Memory mem;
    PhysicalPageManager ppm;
    std::unique_ptr<UserAddressSpace> uas;

    const std::uint64_t rw_page_va{0x20000ULL};
    const std::uint64_t ro_page_va{0x40000ULL};
    const std::uint64_t unmapped_page_va{0x80000ULL};

    MemoryFixture()
        : mem(0x1000000ULL), // 16 MB physical RAM
          ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL) {
        ppm.initialize(false);

        const std::uint64_t root_l2_pa = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "test_uas");

        // Map read-write page (16 KB)
        const std::uint64_t rw_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(rw_page_va, rw_pa, PagePermissions::UserReadWrite));

        // Map read-only page (16 KB)
        const std::uint64_t ro_pa = ppm.allocate_page();
        TEST_ASSERT(uas->map_user_page(ro_page_va, ro_pa, PagePermissions::UserRead));
    }
};

static void test_null_pointer_validation() {
    std::cout << "[Stage 232.15] Testing null pointer validation across all validation primitives...\n";
    MemoryFixture f;
    std::uint8_t buf[64]{};

    // Buffer validation with null pointer
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, 0, 64, false) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, 0, 64, true) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, 0, 64) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, 0, 64) == ValidationResult::NullPointer);

    // copyin / copyout with null pointers
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, 0, buf, sizeof(buf)) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.rw_page_va, nullptr, sizeof(buf)) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, 0, sizeof(buf)) == ValidationResult::NullPointer);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, nullptr, f.rw_page_va, sizeof(buf)) == ValidationResult::NullPointer);

    // copy_string with null pointer
    std::string str;
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, 0, 1024, str) == ValidationResult::NullPointer);
}

static void test_address_space_boundaries_and_overflow() {
    std::cout << "[Stage 232.15] Testing address space limits and integer overflow...\n";
    MemoryFixture f;
    std::uint8_t buf[64]{};

    // Wrap-around / integer overflow: 0xFFFFFFFFFFFFFFF0 + 32 wraps around 64-bit space
    const std::uint64_t overflow_va = 0xFFFFFFFFFFFFFFF0ULL;
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, overflow_va, 32, false) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, overflow_va, 32, true) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, overflow_va, buf, 32) == ValidationResult::IntegerOverflow);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, overflow_va, 32) == ValidationResult::IntegerOverflow);

    // Kernel/EL1 space limit violation (>= kUserSpaceLimitVa = 0x0001000000000000ULL)
    const std::uint64_t kernel_va = kUserSpaceLimitVa;
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, kernel_va, 16, false) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::validate_buffer(*f.uas, kernel_va, 16, true) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, kernel_va, buf, 16) == ValidationResult::OutOfBounds);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, kernel_va, 16) == ValidationResult::OutOfBounds);

    std::string str;
    TEST_ASSERT(GuestMemoryValidator::copy_string(*f.uas, kernel_va, 1024, str) == ValidationResult::OutOfBounds);
}

static void test_unmapped_and_permission_checks() {
    std::cout << "[Stage 232.15] Testing unmapped pages and read-only permission enforcement...\n";
    MemoryFixture f;
    std::uint8_t buf[64]{0xAA};

    // Unmapped page checks
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, f.unmapped_page_va, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, f.unmapped_page_va, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.unmapped_page_va, buf, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, f.unmapped_page_va, 64) == ValidationResult::UnmappedPage);

    // Read-only page checks
    // Reading from read-only page must succeed
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, f.ro_page_va, 64) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.ro_page_va, buf, 64) == ValidationResult::Success);

    // Writing to read-only page must be strictly rejected with PermissionDenied
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, f.ro_page_va, 64) == ValidationResult::PermissionDenied);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, f.ro_page_va, 64) == ValidationResult::PermissionDenied);

    // Read-write page checks: both read and write succeed
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, f.rw_page_va, 64) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, f.rw_page_va, 64) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, f.rw_page_va, 64) == ValidationResult::Success);

    std::uint8_t readback[64]{};
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, f.rw_page_va, readback, 64) == ValidationResult::Success);
    TEST_ASSERT(std::memcmp(buf, readback, 64) == 0);
}

static void test_string_null_termination_defense() {
    std::cout << "[Stage 232.15] Testing SEC-232-05: String null termination defense...\n";
    MemoryFixture f;

    // Fill rw_page with non-zero characters (all 'X')
    std::vector<std::uint8_t> non_null_data(1024, 'X');
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, non_null_data.data(), f.rw_page_va, non_null_data.size()) == ValidationResult::Success);

    // Reading 1024 bytes when no null terminator exists must return StringNotTerminated!
    std::string out_str;
    auto res = GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 1024, out_str);
    TEST_ASSERT(res == ValidationResult::StringNotTerminated);

    // Now write a valid null-terminated string at offset 0: "HelloWinIOS\0"
    const char greeting[] = "HelloWinIOS";
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, greeting, f.rw_page_va, sizeof(greeting)) == ValidationResult::Success);

    // Reading with max_bytes = 1024 must successfully return the terminated string
    std::string good_str;
    res = GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 1024, good_str);
    TEST_ASSERT(res == ValidationResult::Success);
    TEST_ASSERT(good_str == "HelloWinIOS");

    // Reading with max_bytes smaller than the string length must return StringNotTerminated
    std::string truncated_str;
    res = GuestMemoryValidator::copy_string(*f.uas, f.rw_page_va, 5, truncated_str);
    TEST_ASSERT(res == ValidationResult::StringNotTerminated);
}

static void test_multi_page_boundary_crossing() {
    std::cout << "[Stage 232.15] Testing multi-page boundary crossing and partial unmapped rejection...\n";
    MemoryFixture f;

    // rw_page_va is mapped at 0x20000 (16KB granule: [0x20000, 0x24000))
    // 0x24000 is NOT mapped!
    // Allocate a buffer spanning from (0x24000 - 32) to (0x24000 + 32): 64 bytes total
    const std::uint64_t spanning_va = 0x24000ULL - 32ULL;
    std::uint8_t buf[64]{};

    // Range starts in mapped page but crosses into unmapped page: must fail with UnmappedPage!
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, spanning_va, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, spanning_va, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, spanning_va, buf, 64) == ValidationResult::UnmappedPage);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, spanning_va, 64) == ValidationResult::UnmappedPage);

    // Now map second page at 0x24000 as read-only
    const std::uint64_t ro_second_pa = f.ppm.allocate_page();
    TEST_ASSERT(f.uas->map_user_page(0x24000ULL, ro_second_pa, PagePermissions::UserRead));

    // Reading across both mapped pages now succeeds
    TEST_ASSERT(GuestMemoryValidator::validate_guest_read(*f.uas, spanning_va, 64) == ValidationResult::Success);
    TEST_ASSERT(GuestMemoryValidator::copyin(*f.uas, spanning_va, buf, 64) == ValidationResult::Success);

    // Writing across the boundary must fail with PermissionDenied because the second page is read-only!
    TEST_ASSERT(GuestMemoryValidator::validate_guest_write(*f.uas, spanning_va, 64) == ValidationResult::PermissionDenied);
    TEST_ASSERT(GuestMemoryValidator::copyout(*f.uas, buf, spanning_va, 64) == ValidationResult::PermissionDenied);
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "STAGE 232.15: Syscall Copyin/Copyout Memory Validation Tests\n";
    std::cout << "========================================================\n";

    test_null_pointer_validation();
    test_address_space_boundaries_and_overflow();
    test_unmapped_and_permission_checks();
    test_string_null_termination_defense();
    test_multi_page_boundary_crossing();

    std::cout << "All Stage 232.15 tests passed successfully!\n";
    return 0;
}
