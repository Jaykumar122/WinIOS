#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "graphics/compositor.h"
#include "graphics/display_surface.h"
#include "graphics/window_manager.h"
#include "input/input_router.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "kernel/user_syscall_dispatcher.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/guest_ui_runtime.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::memory;

struct AbiTestHarness {
    Memory mem{0x1000000};
    PhysicalPageManager ppm{mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL};
    KernelThreadManager thread_mgr;
    PriorityScheduler sched{thread_mgr, mem};
    BsdProcessManager proc_mgr;
    BsdProcess* proc{nullptr};
    std::unique_ptr<memory::UserAddressSpace> uas;
    CPU cpu;
    VirtualConsole console;
    UserSyscallDispatcher dispatcher{console};

    std::shared_ptr<VirtualDisplay> virtual_display;
    std::shared_ptr<Compositor> compositor;
    std::unique_ptr<SurfaceResourceManager> surface_mgr;
    std::shared_ptr<WindowManager> window_mgr;
    std::unique_ptr<InputRouter> input_router;

    AbiTestHarness() {
        ppm.initialize(false);
        sched.initialize();
        proc_mgr.initialize();
        proc = proc_mgr.create_process("test_app", 1);
        cpu.reset();

        const std::uint64_t root_l2 = ppm.allocate_page();
        uas = std::make_unique<memory::UserAddressSpace>(mem, ppm, root_l2, "test_vm");

        DisplayConfiguration disp_cfg{};
        disp_cfg.width = 800;
        disp_cfg.height = 1200;
        disp_cfg.row_stride = 800 * 4;
        disp_cfg.pixel_format = PixelFormat::B8G8R8A8_UNORM;
        virtual_display = std::make_shared<VirtualDisplay>(disp_cfg);
        compositor = std::make_shared<Compositor>(virtual_display);
        surface_mgr = std::make_unique<SurfaceResourceManager>();
        window_mgr = std::make_shared<WindowManager>();
        input_router = std::make_unique<InputRouter>(window_mgr);

        dispatcher.set_surface_resource_manager(surface_mgr.get());
        dispatcher.set_window_manager(window_mgr.get());
        dispatcher.set_compositor(compositor.get());
        dispatcher.set_input_router(input_router.get());

        // Map stack and code pages in user space
        const std::uint64_t pa1 = ppm.allocate_page();
        const std::uint64_t pa2 = ppm.allocate_page();
        uas->map_user_page(0x10000, pa1, memory::PagePermissions::UserReadWrite);
        uas->map_user_page(0x14000, pa2, memory::PagePermissions::UserReadWrite);
    }
};

void test_abi_version() {
    AbiTestHarness h;

    // Test Fast ABI version query (Fast ABI: X0=sys_num -> return in X0)
    h.dispatcher.set_abi_mode(SyscallAbi::WinIOSFast);
    h.cpu.write_register(0, kSysWiniosGetAbiVersion);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == kWiniosCategoryCAbiVersion);

    // Test DarwinStrict ABI version query (Darwin: X16=sys_num -> return in X0)
    h.dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    h.cpu.write_register(16, kSysWiniosGetAbiVersion);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == kWiniosCategoryCAbiVersion);
    // Carry bit should NOT be set on success
    assert((h.cpu.get_nzcv() & 0x2u) == 0);

    std::cout << "[PASS] test_abi_version passed\n";
}

void test_unimplemented_syscalls() {
    AbiTestHarness h;
    h.dispatcher.set_abi_mode(SyscallAbi::WinIOSFast);

    // Syscall 508 is outside Category C (Fast ABI: X0=508)
    h.cpu.write_register(0, 508);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    // In Fast ABI, error is -kErrnoENOSYS
    assert(static_cast<std::int64_t>(h.cpu.read_register(0)) == -kErrnoENOSYS);

    // Syscall 999
    h.cpu.write_register(0, 999);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(static_cast<std::int64_t>(h.cpu.read_register(0)) == -kErrnoENOSYS);

    // In DarwinStrict mode:
    h.dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);
    h.cpu.write_register(16, 508);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoENOSYS));
    assert((h.cpu.get_nzcv() & 0x2u) != 0); // Carry bit set

    std::cout << "[PASS] test_unimplemented_syscalls passed\n";
}

void test_window_create_destroy_validation() {
    AbiTestHarness h;
    h.dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    // 1. Invalid dimensions (w=0, h=100) -> EINVAL
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0); // null title
    h.cpu.write_register(1, 0);
    h.cpu.write_register(2, 0); // x
    h.cpu.write_register(3, 0); // y
    h.cpu.write_register(4, 0); // w = 0
    h.cpu.write_register(5, 100); // h = 100
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 2. Unmapped title VA -> EFAULT
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0x90000); // unmapped VA
    h.cpu.write_register(1, 16);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 200);
    h.cpu.write_register(5, 200);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 3. Valid creation with default title
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0); // default title
    h.cpu.write_register(1, 0);
    h.cpu.write_register(2, 10);
    h.cpu.write_register(3, 20);
    h.cpu.write_register(4, 100);
    h.cpu.write_register(5, 100);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert((h.cpu.get_nzcv() & 0x2u) == 0);
    std::uint64_t wid = h.cpu.read_register(0);
    assert(wid > 0);

    // 4. Destroy non-existent window -> EBADF
    h.cpu.write_register(16, kSysWiniosWindowDestroy);
    h.cpu.write_register(0, 9999);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 5. Destroy valid window -> 0
    h.cpu.write_register(16, kSysWiniosWindowDestroy);
    h.cpu.write_register(0, wid);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == 0);
    assert((h.cpu.get_nzcv() & 0x2u) == 0);

    std::cout << "[PASS] test_window_create_destroy_validation passed\n";
}

void test_surface_update_and_poll_event_validation() {
    AbiTestHarness h;
    h.dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    // Create a 50x50 window
    h.cpu.write_register(16, kSysWiniosWindowCreate);
    h.cpu.write_register(0, 0);
    h.cpu.write_register(1, 0);
    h.cpu.write_register(2, 0);
    h.cpu.write_register(3, 0);
    h.cpu.write_register(4, 50);
    h.cpu.write_register(5, 50);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert((h.cpu.get_nzcv() & 0x2u) == 0);
    std::uint32_t wid = static_cast<std::uint32_t>(h.cpu.read_register(0));
    assert(wid > 0);

    // Surface size is 50 * 50 * 4 = 10,000 bytes (< 16KB, fits in mapped page 0x10000)

    // 1. Surface update with invalid window ID -> EBADF
    h.cpu.write_register(16, kSysWiniosSurfaceUpdate);
    h.cpu.write_register(0, 999);
    h.cpu.write_register(1, 0x10000);
    h.cpu.write_register(2, 10000);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEBADF));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 2. Surface update with size too small -> EINVAL
    h.cpu.write_register(16, kSysWiniosSurfaceUpdate);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, 0x10000);
    h.cpu.write_register(2, 5000);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 3. Surface update with misaligned pointer (0x10001 not 4-byte aligned) -> EFAULT
    h.cpu.write_register(16, kSysWiniosSurfaceUpdate);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, 0x10001);
    h.cpu.write_register(2, 10000);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 4. Surface update with unmapped pointer (0x80000) -> EFAULT
    h.cpu.write_register(16, kSysWiniosSurfaceUpdate);
    h.cpu.write_register(0, wid);
    h.cpu.write_register(1, 0x80000);
    h.cpu.write_register(2, 10000);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 5. Poll event with too small buffer -> EINVAL
    h.cpu.write_register(16, kSysWiniosPollEvent);
    h.cpu.write_register(0, 0x10000);
    h.cpu.write_register(1, sizeof(runtime::GuestInputEvent) - 1);
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEINVAL));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 6. Poll event with misaligned pointer (0x10004 not 8-byte aligned) -> EFAULT
    h.cpu.write_register(16, kSysWiniosPollEvent);
    h.cpu.write_register(0, 0x10004);
    h.cpu.write_register(1, sizeof(runtime::GuestInputEvent));
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    // 7. Poll event with unmapped pointer -> EFAULT
    h.cpu.write_register(16, kSysWiniosPollEvent);
    h.cpu.write_register(0, 0x90000);
    h.cpu.write_register(1, sizeof(runtime::GuestInputEvent));
    h.dispatcher.handle_syscall(h.cpu, *h.proc, *h.uas, h.sched);
    assert(h.cpu.read_register(0) == static_cast<std::uint64_t>(kErrnoEFAULT));
    assert((h.cpu.get_nzcv() & 0x2u) != 0);

    std::cout << "[PASS] test_surface_update_and_poll_event_validation passed\n";
}

int main() {
    std::cout << "Starting Stage 215.3 Category C ABI v1.0 Hardening Tests...\n";
    test_abi_version();
    test_unimplemented_syscalls();
    test_window_create_destroy_validation();
    test_surface_update_and_poll_event_validation();
    std::cout << "All Stage 215.3 ABI Hardening tests PASSED!\n";
    return 0;
}
