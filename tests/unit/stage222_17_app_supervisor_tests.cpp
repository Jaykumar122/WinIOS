#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "kernel/app_lifecycle_manager.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "graphics/compositor.h"
#include "graphics/virtual_display.h"
#include "input/input_router.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "security/capability_manager.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "runtime/interactive_runtime_contract.h"

using namespace winios;
using namespace winios::kernel;
using namespace winios::graphics;
using namespace winios::input;
using namespace winios::storage;
using namespace winios::security;

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

void test_supervisor_lifecycle_and_fault_cleanup() {
    std::cout << "[Test 1] Supervisor lifecycle state machine & fault teardown...\n";
    auto wm = std::make_shared<WindowManager>();
    auto sm = std::make_shared<SurfaceResourceManager>();
    DisplayConfiguration cfg{};
    cfg.width = 400; cfg.height = 400; cfg.row_stride = 1600;
    auto display = std::make_shared<VirtualDisplay>(cfg);
    auto comp = std::make_shared<Compositor>(display);
    auto router = std::make_shared<InputRouter>(wm);

    AppLifecycleManager supervisor(wm.get(), sm.get(), comp.get(), router.get());

    // 1. Register App A (PID 10)
    TEST_ASSERT(supervisor.register_app(10, "com.apple.testapp"));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Created);

    // 2. Transition to Foreground
    TEST_ASSERT(supervisor.transition_to(10, AppState::Starting));
    TEST_ASSERT(supervisor.transition_to(10, AppState::Foreground));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Foreground);
    TEST_ASSERT(supervisor.foreground_pid() == 10);

    // Create window and surface for App A
    std::shared_ptr<DisplaySurface> surf;
    auto res_code = sm->create_surface(10, 200, 200, PixelFormat::B8G8R8A8_UNORM, surf);
    TEST_ASSERT(res_code == SurfaceResultCode::Success);
    comp->add_surface(surf);
    uint32_t wid = wm->create_window(10, "TestAppWindow", Rect{10, 10, 200, 200}, surf);
    supervisor.associate_window(10, wid);
    supervisor.associate_surface(10, surf->id());

    TEST_ASSERT(wm->window_count() == 1);
    TEST_ASSERT(sm->get_process_accounting(10).surface_count == 1);

    // 3. Background, Suspend and Resume
    TEST_ASSERT(supervisor.send_to_background(10));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Background);
    TEST_ASSERT(supervisor.suspend_app(10));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Suspended);
    TEST_ASSERT(supervisor.resume_app(10));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Foreground);

    // 4. Fault Teardown: App crashes/terminates
    TEST_ASSERT(supervisor.terminate_app(10));
    TEST_ASSERT(supervisor.get_app_state(10) == AppState::Terminated);

    // Resources must be cleaned up
    TEST_ASSERT(wm->window_count() == 0);
    TEST_ASSERT(sm->get_process_accounting(10).surface_count == 0);

    std::cout << "[PASS] Supervisor lifecycle and fault teardown verified.\n";
}

void test_supervisor_notify_syscall() {
    std::cout << "[Test 2] kSysWiniosAppSupervisorNotify syscall integration...\n";
    Memory mem(0x800000);
    VirtualConsole console;
    UserSyscallDispatcher disp(console);
    disp.set_abi_mode(SyscallAbi::DarwinStrict);

    AppLifecycleManager supervisor;
    supervisor.launch_app(101, "com.apple.notifyme");
    disp.set_lifecycle_manager(&supervisor);

    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "uas_sup_test");

    BsdProcess proc(101, 1, "notify_app");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);
    CPU cpu;

    // Call kSysWiniosAppSupervisorNotify (536) to notify Background (3)
    cpu.write_register(16, kSysWiniosAppSupervisorNotify);
    cpu.write_register(0, 3); // Background
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Background);

    // Call to notify Suspended (4)
    cpu.write_register(16, kSysWiniosAppSupervisorNotify);
    cpu.write_register(0, 4); // Suspended
    TEST_ASSERT(disp.handle_syscall(cpu, proc, uas, sched));
    TEST_ASSERT(cpu.read_register(0) == 0);
    TEST_ASSERT(supervisor.get_app_state(101) == AppState::Suspended);

    std::cout << "[PASS] Supervisor notification syscall verified.\n";
}

void test_persistent_state_and_data_isolation() {
    std::cout << "[Test 3] Persistent state save/restore & directory sandbox isolation...\n";
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();

    // App A: Notes App
    IsolatedStorage notes_store(vfs, "com.apple.mobilenotes");
    const char* notes_data = "{\"version\": 1, \"active_note\": \"Meeting Notes Campaign 222\"}";
    auto write_res = notes_store.atomic_write("state.json", notes_data, std::strlen(notes_data) + 1);
    TEST_ASSERT(write_res == StorageResult::Success);

    // Re-read and verify contents
    std::vector<uint8_t> read_data;
    auto read_res = notes_store.read("state.json", read_data);
    TEST_ASSERT(read_res == StorageResult::Success);
    TEST_ASSERT(std::strcmp(reinterpret_cast<const char*>(read_data.data()), notes_data) == 0);

    // Sandbox directory traversal prevention
    TEST_ASSERT(!notes_store.is_path_safe("../foreign_app/secret.txt"));
    TEST_ASSERT(!notes_store.is_path_safe("/etc/passwd"));
    TEST_ASSERT(notes_store.is_path_safe("documents/sub/note1.txt"));

    // App B: Runtime Monitor (isolated sandbox)
    IsolatedStorage monitor_store(vfs, "com.apple.runtimemonitor");
    std::vector<uint8_t> foreign_read;
    // App B cannot read App A's private state.json because its root is /data/com.apple.runtimemonitor/
    read_res = monitor_store.read("state.json", foreign_read);
    TEST_ASSERT(read_res == StorageResult::FileNotFound);

    std::cout << "[PASS] Persistent state and sandbox isolation verified.\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.17 & 222.18: SUPERVISOR & PERSISTENCE TESTS\n";
    std::cout << "========================================\n";

    test_supervisor_lifecycle_and_fault_cleanup();
    test_supervisor_notify_syscall();
    test_persistent_state_and_data_isolation();

    std::cout << "\nALL STAGE 222.17 & 222.18 TESTS PASSED (100%).\n";
    return 0;
}
