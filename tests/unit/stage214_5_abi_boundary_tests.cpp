// ============================================================================
// Stage 214.5: Darwin ABI and Custom Extension Boundary Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_surface.h"
#include "graphics/virtual_display.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

using namespace winios;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::kernel;
using namespace winios::memory;

namespace {

int g_failures = 0;

#define ABI_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ABI_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [ABI_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_abi_boundaries() {
    std::cout << "[Stage 214.5] Verifying Darwin ABI and Category C Boundary...\n";

    // 1. Version constant and category classification
    ABI_CHECK(kWiniosCategoryCAbiVersion >= 0x00010000, "Category C ABI Version is at least 1.0 (0x00010000)");
    ABI_CHECK(!is_category_c_extension(1), "kSysExit (1) is not Category C");
    ABI_CHECK(!is_category_c_extension(4), "kSysWrite (4) is not Category C");
    ABI_CHECK(!is_category_c_extension(329), "kSysSchedYield (329) is not Category C");
    ABI_CHECK(!is_category_c_extension(440), "Standard Darwin max is not Category C");
    ABI_CHECK(is_category_c_extension(500), "SYS_winios_window_create (500) is Category C");
    ABI_CHECK(is_category_c_extension(506), "SYS_winios_set_window_title (506) is Category C");
    ABI_CHECK(is_category_c_extension(507), "SYS_winios_get_abi_version (507) is Category C");
    ABI_CHECK(!is_category_c_extension(kSysWiniosMaxCategoryC + 1), "Out of range syscall is not Category C");

    // 2. Setup mock environment
    Memory mem(0x1000000);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* procA = proc_mgr.create_process("procA", 1);
    auto* procB = proc_mgr.create_process("procB", 2);

    const std::uint64_t root_l2_A = ppm.allocate_page();
    UserAddressSpace uasA(mem, ppm, root_l2_A, "procA_vm");

    CPU cpu;
    cpu.reset();

    VirtualConsole console;
    UserSyscallDispatcher disp(console);

    auto window_mgr = std::make_shared<WindowManager>();
    auto surface_mgr = std::make_unique<SurfaceResourceManager>();
    disp.set_window_manager(window_mgr.get());
    disp.set_surface_resource_manager(surface_mgr.get());

    // 3. Test Unknown Syscall in DarwinStrict mode
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    cpu.write_register(16, 999); // Unknown syscall
    disp.handle_syscall(cpu, *procA, uasA, sched);

    // Darwin ABI returns errno in X0, sets Carry bit (PSTATE.C = 1)
    ABI_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS), "Unknown syscall returned ENOSYS (78)");
    ABI_CHECK((cpu.get_nzcv() & 0x2u) != 0, "DarwinStrict mode set Carry flag on error");

    // 4. Test Unknown Syscall in WinIOSFast mode
    disp.set_abi_mode(SyscallAbi::WinIOSFast);
    cpu.write_register(0, 999); // Unknown syscall in X0
    disp.handle_syscall(cpu, *procA, uasA, sched);

    // Fast ABI returns negative errno in X0
    ABI_CHECK(static_cast<std::int64_t>(cpu.read_register(0)) == -static_cast<std::int64_t>(kErrnoENOSYS),
              "WinIOSFast mode returned -ENOSYS (-78) in X0");

    // 5. Test Parameter Validation: Window Create with w=0
    disp.set_abi_mode(SyscallAbi::DarwinStrict);
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0); // No title
    cpu.write_register(1, 0);
    cpu.write_register(2, 50); // x
    cpu.write_register(3, 50); // y
    cpu.write_register(4, 0);  // width = 0 (INVALID!)
    cpu.write_register(5, 100);// height
    disp.handle_syscall(cpu, *procA, uasA, sched);

    ABI_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL), "Zero width rejected with EINVAL");
    ABI_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry bit set on EINVAL error");

    // 6. Test Valid Window Creation
    cpu.write_register(16, kSysWiniosWindowCreate);
    cpu.write_register(0, 0);
    cpu.write_register(1, 0);
    cpu.write_register(2, 50);
    cpu.write_register(3, 50);
    cpu.write_register(4, 200); // width
    cpu.write_register(5, 300); // height
    disp.handle_syscall(cpu, *procA, uasA, sched);

    ABI_CHECK((cpu.get_nzcv() & 0x2u) == 0, "Carry bit cleared on success");
    const std::uint64_t win_id_A = cpu.read_register(0);
    ABI_CHECK(win_id_A > 0, "Valid window ID returned for procA");

    // 7. Cross-Process Isolation: procB attempts to destroy procA's window
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, win_id_A);
    disp.handle_syscall(cpu, *procB, uasA, sched);

    ABI_CHECK(cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF),
              "Cross-process window destruction rejected with EBADF");
    ABI_CHECK((cpu.get_nzcv() & 0x2u) != 0, "Carry bit set on permission rejection");

    // 8. Legitimate destruction by owner
    cpu.write_register(16, kSysWiniosWindowDestroy);
    cpu.write_register(0, win_id_A);
    disp.handle_syscall(cpu, *procA, uasA, sched);

    ABI_CHECK((cpu.get_nzcv() & 0x2u) == 0, "Owner successfully destroyed window");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 214.5: Darwin ABI Boundary Tests      \n";
    std::cout << "====================================================\n";

    test_abi_boundaries();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL ABI TESTS PASSED" : "ABI FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
