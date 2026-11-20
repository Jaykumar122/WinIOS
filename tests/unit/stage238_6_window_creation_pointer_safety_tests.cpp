// ============================================================================
// Stage 238.6: Window Creation and Guest Pointer Safety Tests
// ============================================================================
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
#include <string>

#include "tests/support/test_assert.h"
#include "cpu/cpu.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_event_services.h"
#include "winios_sdk/winios_sdk.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::runtime;
using namespace winios::memory;

namespace {

struct Harness {
    Memory mem{0x2000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0x1FC0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    VirtualConsole console;
    UserSyscallDispatcher dispatcher{console};
    std::shared_ptr<WindowManager> wm;
    std::shared_ptr<SurfaceResourceManager> srm;
    std::shared_ptr<InputRouter> router;
    std::shared_ptr<GuestEventServices> ev_svc;
    CPU cpu;
    std::unique_ptr<UserAddressSpace> uas;
    std::uint64_t valid_title_va{0x20000};

    Harness() {
        ppm.initialize(false);
        sched.initialize();
        cpu.reset();

        wm = std::make_shared<WindowManager>();
        srm = std::make_shared<SurfaceResourceManager>();
        router = std::make_shared<InputRouter>(wm);
        ev_svc = std::make_shared<GuestEventServices>(router);

        dispatcher.set_window_manager(wm.get());
        dispatcher.set_surface_resource_manager(srm.get());
        dispatcher.set_input_router(router.get());
        dispatcher.set_event_services(ev_svc.get());
        dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

        const std::uint64_t root_l2 = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2, "harness_vm");

        const std::uint64_t pa = ppm.allocate_page();
        uas->map_user_page(valid_title_va, pa, PagePermissions::UserReadWrite);
    }
};

void test_null_title_pointer_defaults_cleanly() {
    std::cout << "[Test 1] Null Title Pointer (title_va == 0) -> Clean Default Title...\n";
    Harness h;
    BsdProcess proc(10, 1, "App", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0); // null pointer
    h.cpu.write_register(1, 0); // len
    h.cpu.write_register(2, 50); // x
    h.cpu.write_register(3, 50); // y
    h.cpu.write_register(4, 300); // width
    h.cpu.write_register(5, 200); // height

    TEST_ASSERT(h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    std::uint32_t wid = static_cast<std::uint32_t>(h.cpu.read_register(0));
    TEST_ASSERT(wid > 0);

    auto win = h.wm->get_window(wid);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->title == "WinIOS App", "Null pointer must safely default to 'WinIOS App'");
    std::cout << "  -> Passed null title pointer default test\n";
}

void test_valid_title_string_copied() {
    std::cout << "[Test 2] Valid Title String -> Copied Correctly...\n";
    Harness h;
    BsdProcess proc(10, 1, "App", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    const char custom_title[] = "My Custom Window Title";
    TEST_ASSERT(h.uas->copyin(h.valid_title_va, custom_title, sizeof(custom_title)));

    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, h.valid_title_va);
    h.cpu.write_register(1, sizeof(custom_title));
    h.cpu.write_register(2, 10);
    h.cpu.write_register(3, 20);
    h.cpu.write_register(4, 400);
    h.cpu.write_register(5, 300);

    TEST_ASSERT(h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched));
    std::uint32_t wid = static_cast<std::uint32_t>(h.cpu.read_register(0));
    auto win = h.wm->get_window(wid);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(win->title == "My Custom Window Title");
    std::cout << "  -> Passed valid title string test\n";
}

void test_invalid_non_null_pointer_rejected() {
    std::cout << "[Test 3] Invalid Non-Null Pointer -> Strict EFAULT Rejection...\n";
    Harness h;
    BsdProcess proc(10, 1, "App", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    std::uint64_t initial_invalid_ptrs = h.dispatcher.stats().total_invalid_pointers;

    // Unmapped pointer
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0x88880000ULL); // unmapped VA
    h.cpu.write_register(1, 64);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 200);
    h.cpu.write_register(5, 200);

    bool ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Window creation with unmapped title pointer must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT),
                "Unmapped pointer must fail with EFAULT, never silently default to 'WinIOS App'");
    TEST_ASSERT(h.dispatcher.stats().total_invalid_pointers > initial_invalid_ptrs);
    TEST_ASSERT(h.wm->window_count() == 0, "No leaked window on EFAULT failure");

    // Out-of-bounds pointer (> 32 GiB limit)
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0x10000000000ULL); // out-of-bounds
    h.cpu.write_register(1, 64);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 200);
    h.cpu.write_register(5, 200);

    ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok, "Out-of-bounds title pointer must fail");
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    std::cout << "  -> Passed invalid non-null pointer EFAULT rejection\n";
}

void test_zero_dimensions_rejected() {
    std::cout << "[Test 4] Zero Dimension Validation (EINVAL)...\n";
    Harness h;
    BsdProcess proc(10, 1, "App", nullptr);
    proc.set_category_c_abi_version(kWiniosCategoryCAbi_v1_1);

    // width = 0
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0);
    h.cpu.write_register(1, 0);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 0); // width = 0
    h.cpu.write_register(5, 100);

    bool ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok);
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));

    // height = 0
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0);
    h.cpu.write_register(1, 0);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 100);
    h.cpu.write_register(5, 0); // height = 0

    ok = h.dispatcher.handle_syscall(h.cpu, proc, *h.uas, h.sched);
    TEST_ASSERT(!ok);
    TEST_ASSERT(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));
    std::cout << "  -> Passed zero dimensions EINVAL test\n";
}

} // namespace

int main() {
    std::cout << "========================================================\n";
    std::cout << " Stage 238.6: Window Creation & Pointer Safety Suite    \n";
    std::cout << "========================================================\n";

    test_null_title_pointer_defaults_cleanly();
    test_valid_title_string_copied();
    test_invalid_non_null_pointer_rejected();
    test_zero_dimensions_rejected();

    std::cout << "\nALL STAGE 238.6 TESTS COMPLETED SUCCESSFULLY!\n";
    return 0;
}
