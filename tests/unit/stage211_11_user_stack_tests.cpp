#include <iostream>
#include <vector>
#include <cstdint>

#include "kernel/user_stack.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::kernel;
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

void test_user_stack_construction() {
    std::cout << "[Test 1] Stage 211.11: User Stack Layout & AAPCS64 16-Byte Alignment...\n";

    Memory mem(0x800000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "stack_test");

    UserStackConfig cfg;
    cfg.stack_top_va = 0x70000ULL;
    cfg.stack_size = 0x10000; // 64 KiB
    cfg.argv = {"my_app", "--test", "arg3"};
    cfg.envp = {"HOME=/root", "TERM=vt100"};

    auto res = UserStackBuilder::build_stack(uas, ppm, mem, cfg);
    TEST_CHECK(res.success, "User stack successfully constructed");
    TEST_CHECK(res.argc == 3, "argc is 3");
    TEST_CHECK(res.stack_bottom_va == 0x60000ULL, "Stack bottom is 0x60000");

    // AAPCS64 requires SP aligned to 16 bytes
    TEST_CHECK((res.final_sp_el0 % 16) == 0, "final_sp_el0 is 16-byte aligned");
    TEST_CHECK(res.final_sp_el0 >= res.stack_bottom_va && res.final_sp_el0 < cfg.stack_top_va,
               "final_sp_el0 within allocated stack boundaries");

    // Read argc at final_sp_el0
    std::uint64_t read_argc = 0;
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(res.final_sp_el0), &read_argc, 8), "Read argc from stack");
    TEST_CHECK(read_argc == 3, "Read argc is 3");

    // Read argv pointers
    std::uint64_t arg0_ptr = 0;
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(res.argv_va), &arg0_ptr, 8), "Read argv[0] pointer");
    char arg0_str[16]{0};
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(arg0_ptr), arg0_str, 7), "Read argv[0] string");
    TEST_CHECK(std::string(arg0_str) == "my_app", "argv[0] string matches 'my_app'");

    std::uint64_t arg1_ptr = 0;
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(res.argv_va + 8), &arg1_ptr, 8), "Read argv[1] pointer");
    char arg1_str[16]{0};
    TEST_CHECK(uas.copyout(reinterpret_cast<const void*>(arg1_ptr), arg1_str, 7), "Read argv[1] string");
    TEST_CHECK(std::string(arg1_str) == "--test", "argv[1] string matches '--test'");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 211.11 User Stack Tests ===\n";
    test_user_stack_construction();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 211.11: All user stack tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 211.11: " << g_failures << " tests failed.\n";
        return 1;
    }
}
