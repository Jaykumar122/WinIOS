#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "kernel/user_syscall_dispatcher.h"
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"
#include "runtime/system_clipboard.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::security;
using namespace winios::storage;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_adv_foreign_window_manipulation() {
    std::cout << "[ADV-1] Foreign window manipulation & hijack prevention...\n";
    WindowManager wm;
    SurfaceResourceManager sm;

    std::shared_ptr<DisplaySurface> surf1;
    auto rc = sm.create_surface(100, 200, 200, PixelFormat::B8G8R8A8_UNORM, surf1);
    TEST_ASSERT(rc == SurfaceResultCode::Success);
    uint32_t wid1 = wm.create_window(100, "App100Win", Rect{10, 10, 200, 200}, surf1);
    TEST_ASSERT(wid1 > 0);

    // PID 200 attempts to destroy PID 100's window
    auto win = wm.get_window(wid1);
    TEST_ASSERT(win != nullptr);
    TEST_ASSERT(!win->can_access(200));
    TEST_ASSERT(win->can_access(100));

    // Calling wm.destroy_window(caller_pid=200, wid1) must fail
    TEST_ASSERT(!wm.destroy_window(200, wid1));
    TEST_ASSERT(wm.window_count() == 1);

    // PID 200 attempts to alter geometry
    TEST_ASSERT(!wm.set_window_frame(200, wid1, Rect{500, 500, 100, 100}));
    TEST_ASSERT(win->frame.x == 10 && win->frame.y == 10);

    // Legitimate owner PID 100 succeeds in changing frame & destroying
    TEST_ASSERT(wm.set_window_frame(100, wid1, Rect{15, 15, 200, 200}));
    TEST_ASSERT(win->frame.x == 15 && win->frame.y == 15);
    TEST_ASSERT(wm.destroy_window(100, wid1));
    TEST_ASSERT(wm.window_count() == 0);

    std::cout << "[PASS] Foreign window manipulation rejected.\n";
}

void test_adv_clipboard_capability_enforcement() {
    std::cout << "[ADV-2] Clipboard capability enforcement & permission violations...\n";
    Memory mem(0x1000000ULL);
    VirtualConsole console;
    UserSyscallDispatcher disp(console);

    auto cm = std::make_shared<CapabilityManager>();
    // Grant write only to PID 100
    cm->grant(100, Capability::ClipboardWrite);
    // PID 200 has NO clipboard permissions
    disp.set_capability_manager(cm.get());

    runtime::SystemClipboard clipboard(cm);
    disp.set_system_clipboard(&clipboard);

    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    // Setup PID 200 UAS
    const uint64_t l2_200 = ppm.allocate_page();
    memory::UserAddressSpace uas200(mem, ppm, l2_200, "uas_200");
    uas200.set_owner_pid(200);
    BsdProcess proc200(200, 1, "Attacker");
    proc200.set_address_space(&uas200);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // 1. PID 200 attempts to write to clipboard without ClipboardWrite
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, 0x20000000ULL);
    cpu.write_register(1, 16);
    disp.handle_syscall(cpu, proc200, uas200, sched);
    // Must fail with error
    TEST_ASSERT(cpu.read_register(0) != 0);

    // 2. PID 200 attempts to read clipboard without ClipboardRead
    cpu.write_register(16, kSysWiniosClipboardGet);
    cpu.write_register(0, 0x20000000ULL);
    cpu.write_register(1, 16);
    disp.handle_syscall(cpu, proc200, uas200, sched);
    TEST_ASSERT(cpu.read_register(0) != 0);

    std::cout << "[PASS] Unauthorized clipboard access prevented.\n";
}

void test_adv_malformed_syscall_arguments() {
    std::cout << "[ADV-3] Malformed syscall arguments, out-of-bounds pointers, overflow...\n";
    Memory mem(0x1000000ULL);
    VirtualConsole console;
    UserSyscallDispatcher disp(console);

    auto cm = std::make_shared<CapabilityManager>();
    cm->grant(300, Capability::ClipboardWrite);
    cm->grant(300, Capability::ClipboardRead);
    disp.set_capability_manager(cm.get());

    runtime::SystemClipboard clipboard(cm);
    disp.set_system_clipboard(&clipboard);

    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    const uint64_t l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, l2, "uas_300");
    uas.set_owner_pid(300);
    BsdProcess proc(300, 1, "Fuzzer");
    proc.set_address_space(&uas);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // 1. Null pointer for clipboard set
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, 0); // Null pointer
    cpu.write_register(1, 64);
    disp.handle_syscall(cpu, proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) != 0); // Must be EFAULT

    // 2. Unmapped virtual address
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, 0xDEADBEEF0000ULL); // Unmapped address
    cpu.write_register(1, 64);
    disp.handle_syscall(cpu, proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) != 0); // Must be EFAULT

    // 3. Gigantic buffer length > 64 KB limit
    cpu.write_register(16, kSysWiniosClipboardSet);
    cpu.write_register(0, 0x20000000ULL);
    cpu.write_register(1, 1024 * 1024 * 100); // 100 MB buffer
    disp.handle_syscall(cpu, proc, uas, sched);
    TEST_ASSERT(cpu.read_register(0) != 0); // Must be EINVAL

    std::cout << "[PASS] Malformed syscall pointers and sizes rejected.\n";
}

void test_adv_surface_limits_and_storage_sandbox() {
    std::cout << "[ADV-4 & ADV-5] Surface exhaustion limits & sandbox directory traversal...\n";
    SurfaceResourceManager sm;
    std::shared_ptr<DisplaySurface> surf;

    // Zero dimension surface rejected
    auto rc0 = sm.create_surface(400, 0, 100, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(rc0 != SurfaceResultCode::Success);

    // Overly large dimensions rejected
    auto rclarge = sm.create_surface(400, 100000, 100000, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(rclarge != SurfaceResultCode::Success);

    // Storage path traversal attacks
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    IsolatedStorage store(vfs, "com.apple.testapp");

    TEST_ASSERT(!store.is_path_safe("../../../etc/passwd"));
    TEST_ASSERT(!store.is_path_safe("/data/com.apple.otherapp/key.pem"));
    TEST_ASSERT(!store.is_path_safe("documents/../../secret.txt"));
    TEST_ASSERT(store.is_path_safe("documents/valid_file.txt"));

    std::cout << "[PASS] Surface limits and sandbox path traversal guards verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.23: ADVERSARIAL SECURITY & BOUNDARY TESTS\n";
    std::cout << "========================================\n";

    test_adv_foreign_window_manipulation();
    test_adv_clipboard_capability_enforcement();
    test_adv_malformed_syscall_arguments();
    test_adv_surface_limits_and_storage_sandbox();

    std::cout << "\nALL STAGE 222.23 ADVERSARIAL TESTS PASSED (100%).\n";
    return 0;
}
