#include <iostream>
#include <vector>
#include <cstdint>

#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_user_address_space_mappings() {
    std::cout << "[Test 1] Stage 211.7: UserAddressSpace Page Mapping & Permission Boundaries...\n";

    Memory mem(0x800000); // 8 MiB RAM
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_user_uas");

    // 1. Map user executable page: VA 0x10000 -> PA 0x44000 (UserExecute)
    const std::uint64_t code_pa = ppm.allocate_page();
    TEST_CHECK(uas.map_user_page(0x10000ULL, code_pa, PagePermissions::UserExecute),
               "Map VA 0x10000 UserExecute");

    // 2. Map user read-write data page: VA 0x20000 -> PA 0x48000 (UserReadWrite)
    const std::uint64_t data_pa = ppm.allocate_page();
    TEST_CHECK(uas.map_user_page(0x20000ULL, data_pa, PagePermissions::UserReadWrite),
               "Map VA 0x20000 UserReadWrite");

    // 3. Reject mapping with kernel-only permissions
    const std::uint64_t dummy_pa = ppm.allocate_page();
    TEST_CHECK(!uas.map_user_page(0x30000ULL, dummy_pa, PagePermissions::KernelReadWrite),
               "Kernel permissions in user address space rejected");

    // 4. Reject mapping beyond user space limit (e.g. >= 2 GiB)
    TEST_CHECK(!uas.map_user_page(kUserSpaceLimitVa, dummy_pa, PagePermissions::UserReadWrite),
               "Mapping beyond user space limit rejected");

    // 5. Translation walk verifications
    PageDescriptorFields f_code{};
    const std::uint64_t trans_code = uas.translate(0x10000ULL, &f_code);
    TEST_CHECK(trans_code == code_pa, "Translated code PA matches");
    TEST_CHECK(f_code.valid && f_code.el0_accessible && f_code.user_execute, "Code page is EL0 executable");
    TEST_CHECK(!f_code.writable, "Code page is read-only / execute");

    PageDescriptorFields f_data{};
    const std::uint64_t trans_data = uas.translate(0x20000ULL, &f_data);
    TEST_CHECK(trans_data == data_pa, "Translated data PA matches");
    TEST_CHECK(f_data.valid && f_data.el0_accessible && f_data.writable, "Data page is EL0 writable");
    TEST_CHECK(!f_data.user_execute, "Data page is non-executable");

    // Unmapped lookup returns 0
    TEST_CHECK(uas.translate(0x50000ULL) == 0, "Unmapped VA translates to 0");
}

void test_buffer_validation_and_data_transfer() {
    std::cout << "[Test 2] Stage 211.7: Buffer Validation & copyin/copyout Data Transfer...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "transfer_test");

    const std::uint64_t page_pa = ppm.allocate_page();
    uas.map_user_page(0x20000ULL, page_pa, PagePermissions::UserReadWrite);

    // Buffer validation tests
    TEST_CHECK(uas.validate_user_buffer(0x20000ULL, 128, false), "Mapped readable buffer valid");
    TEST_CHECK(uas.validate_user_buffer(0x20000ULL, 128, true), "Mapped writable buffer valid");
    TEST_CHECK(!uas.validate_user_buffer(0x10000ULL, 128, false), "Unmapped buffer rejected");
    TEST_CHECK(!uas.validate_user_buffer(0xFFFFFFFFFFFFF000ULL, 0x2000, false), "Overflowing buffer rejected");

    // copyin: Host string -> Guest memory
    const std::string host_msg = "Hello WinIOS Guest!";
    TEST_CHECK(uas.copyin(0x20040ULL, host_msg.c_str(), host_msg.size() + 1), "copyin host string");

    // copyout: Guest memory -> Host string
    char host_buf[64]{0};
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(0x20040ULL), host_buf, host_msg.size() + 1), "copyout to host");
    TEST_CHECK(std::string(host_buf) == host_msg, "Data transferred bit-exact");

    // User stack allocation test
    std::uint64_t stack_bottom = 0;
    TEST_CHECK(uas.allocate_user_stack(0x70000ULL, 0x8000, stack_bottom), "Allocate 32KB user stack");
    TEST_CHECK(stack_bottom == 0x68000ULL, "Stack bottom is 0x68000 (0x70000 - 0x8000)");
    TEST_CHECK(uas.validate_user_buffer(0x68000ULL, 0x8000, true), "Entire stack range is writable");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.7 User Address Space Tests ===\n";
    test_user_address_space_mappings();
    test_buffer_validation_and_data_transfer();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.7: All user address space tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.7: " << g_failures << " tests failed.\n";
        return 1;
    }
}
