#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "vfs/vfs.h"
#include "packaging/app_registry.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "winios_sdk/winios_sdk.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#include <cstring>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::settings;

struct SyscallTestFixture {
    CPU cpu;
    Memory mem;
    PhysicalPageManager ppm;
    KernelThreadManager thread_mgr;
    PriorityScheduler sched;
    VirtualConsole console;
    UserSyscallDispatcher dispatcher;
    std::shared_ptr<vfs::VirtualFileSystem> vfs;
    std::shared_ptr<security::CapabilityManager> cap_mgr;
    BsdProcessManager proc_mgr;
    BsdProcess* proc{nullptr};
    std::shared_ptr<SystemStateManager> state_mgr;
    std::shared_ptr<ControlCenterService> cc_svc;
    std::shared_ptr<TaskManager> task_mgr;
    std::shared_ptr<AccessibilityService> a11y_svc;
    std::shared_ptr<packaging::ApplicationRegistry> app_reg;
    std::shared_ptr<services::DiagnosticsService> diag_svc;
    std::unique_ptr<UserAddressSpace> uas;

    uint64_t valid_page_va{0x30000ULL};
    uint64_t unmapped_page_va{0x99990000ULL};

    SyscallTestFixture()
        : mem(0x1000000), // 16 MB
          ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL),
          sched(thread_mgr, mem),
          dispatcher(console)
    {
        ppm.initialize(false);
        sched.initialize();
        dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

        vfs = std::make_shared<vfs::VirtualFileSystem>();
        cap_mgr = std::make_shared<security::CapabilityManager>();
        dispatcher.set_capability_manager(cap_mgr.get());

        proc_mgr.initialize();
        proc = proc_mgr.create_process("boundary_test_app", 1);
        TEST_ASSERT(proc != nullptr);

        state_mgr = std::make_shared<SystemStateManager>(vfs);
        cc_svc = std::make_shared<ControlCenterService>(state_mgr.get());
        task_mgr = std::make_shared<TaskManager>();
        a11y_svc = std::make_shared<AccessibilityService>(state_mgr.get());
        app_reg = std::make_shared<packaging::ApplicationRegistry>();
        diag_svc = std::make_shared<services::DiagnosticsService>();

        dispatcher.set_system_state_manager(state_mgr.get());
        dispatcher.set_control_center_service(cc_svc.get());
        dispatcher.set_task_manager(task_mgr.get());
        dispatcher.set_accessibility_service(a11y_svc.get());
        dispatcher.set_app_registry(app_reg.get());
        dispatcher.set_diagnostics_service(diag_svc.get());

        const uint64_t root_l2_pa = ppm.allocate_page();
        uas = std::make_unique<UserAddressSpace>(mem, ppm, root_l2_pa, "boundary_uas");
        proc->set_address_space(uas.get());

        const uint64_t data_pa = ppm.allocate_page();
        uas->map_user_page(valid_page_va, data_pa, PagePermissions::UserReadWrite);

        // Register sample app for testing
        packaging::AppRegistration reg_app{};
        reg_app.bundle_id = "com.winios.sample";
        reg_app.name = "Sample";
        reg_app.display_name = "Sample App";
        reg_app.version = "1.0.0";
        reg_app.exec_path = "bin/sample";
        app_reg->register_app(reg_app);
    }
};

// 1. Settings Syscalls Boundary Tests (581..585)
static void test_settings_boundaries() {
    std::cout << "[Test 1] Syscalls 581..585 Settings & Theme boundary safety...\n";
    SyscallTestFixture f;
    f.cap_mgr->grant_mask(f.proc->pid(), 0xFFFFFFFFu); // Grant all caps

    // 581: Null pointer
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, 0);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 581: Unmapped memory
    f.cpu.write_register(16, kSysWiniosSettingsGet);
    f.cpu.write_register(0, f.unmapped_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 582: Null pointer
    f.cpu.write_register(16, kSysWiniosSettingsSet);
    f.cpu.write_register(0, 0);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 583: Theme Get with unmapped pointer
    f.cpu.write_register(16, kSysWiniosThemeGet);
    f.cpu.write_register(0, f.unmapped_page_va);
    f.cpu.write_register(1, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 584: Theme Set with out-of-range enum values
    f.cpu.write_register(16, kSysWiniosThemeSet);
    f.cpu.write_register(0, 9999); // Invalid theme mode
    f.cpu.write_register(1, 0);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    std::cout << "  -> Syscalls 581..585 boundary checks passed.\n";
}

// 2. Multitasking Syscalls Boundary Tests (586..588)
static void test_multitasking_boundaries() {
    std::cout << "[Test 2] Syscalls 586..588 Multitasking boundary safety...\n";
    SyscallTestFixture f;
    f.cap_mgr->grant_mask(f.proc->pid(), 0xFFFFFFFFu);

    // Register a task so task enumeration has at least 1 entry to write
    settings::TaskInfo tinfo{};
    tinfo.pid = 101;
    tinfo.name = "task1";
    tinfo.state = settings::TaskState::Running;
    f.task_mgr->register_task(tinfo);

    // 586: Task Enumerate with null buffer and max_entries > 0
    f.cpu.write_register(16, kSysWiniosTaskEnumerate);
    f.cpu.write_register(0, 0);
    f.cpu.write_register(1, 10);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 586: Task Enumerate with unmapped buffer
    f.cpu.write_register(16, kSysWiniosTaskEnumerate);
    f.cpu.write_register(0, f.unmapped_page_va);
    f.cpu.write_register(1, 10);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 587: Task Switch to non-existent PID
    f.cpu.write_register(16, kSysWiniosTaskSwitch);
    f.cpu.write_register(0, 999999);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 588: Task Terminate non-existent PID
    f.cpu.write_register(16, kSysWiniosTaskTerminate);
    f.cpu.write_register(0, 999999);
    f.cpu.write_register(1, 1);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    std::cout << "  -> Syscalls 586..588 boundary checks passed.\n";
}

// 3. Diagnostics and Application Syscalls Boundary Tests (591..593)
static void test_diag_and_app_boundaries() {
    std::cout << "[Test 3] Syscalls 591..593 Diagnostics & App inspection boundary safety...\n";
    SyscallTestFixture f;
    f.cap_mgr->grant_mask(f.proc->pid(), 0xFFFFFFFFu);

    // Record an event so query has data
    f.diag_svc->record_event(1, 1, 100, 0, "TEST");

    // 591: Null query request pointer
    f.cpu.write_register(16, kSysWiniosDiagEventQuery);
    f.cpu.write_register(0, 0);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 591: Query with unmapped buffer_va
    winios_diag_query_req_t req{};
    req.max_entries = 10;
    req.buffer_va = f.unmapped_page_va;
    TEST_ASSERT(f.uas->copyin(f.valid_page_va, &req, sizeof(req)));

    f.cpu.write_register(16, kSysWiniosDiagEventQuery);
    f.cpu.write_register(0, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 592: App Info with null bundle_id
    f.cpu.write_register(16, kSysWiniosAppInfo);
    f.cpu.write_register(0, 0);
    f.cpu.write_register(1, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 592: App Info with unmapped bundle_id pointer
    f.cpu.write_register(16, kSysWiniosAppInfo);
    f.cpu.write_register(0, f.unmapped_page_va);
    f.cpu.write_register(1, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 592: App Info for non-existent app
    const char missing_bid[] = "com.winios.nonexistent";
    TEST_ASSERT(f.uas->copyin(f.valid_page_va, missing_bid, sizeof(missing_bid)));
    f.cpu.write_register(16, kSysWiniosAppInfo);
    f.cpu.write_register(0, f.valid_page_va);
    f.cpu.write_register(1, f.valid_page_va + 128);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 593: App List with null request pointer
    f.cpu.write_register(16, kSysWiniosAppList);
    f.cpu.write_register(0, 0);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    // 593: App List with unmapped buffer_va
    winios_app_list_req_t list_req{};
    list_req.max_entries = 10;
    list_req.buffer_va = f.unmapped_page_va;
    TEST_ASSERT(f.uas->copyin(f.valid_page_va, &list_req, sizeof(list_req)));
    f.cpu.write_register(16, kSysWiniosAppList);
    f.cpu.write_register(0, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) != 0);

    std::cout << "  -> Syscalls 591..593 boundary checks passed.\n";
}

// 4. Capability Enforcement and Privilege Boundary Tests
static void test_capability_enforcement_boundary() {
    std::cout << "[Test 4] Capability enforcement privilege boundary...\n";
    SyscallTestFixture f;
    // Do NOT grant capabilities to proc (empty capability mask)

    // Syscall 588: Terminate requires TaskManage capability
    f.cpu.write_register(16, kSysWiniosTaskTerminate);
    f.cpu.write_register(0, 102);
    f.cpu.write_register(1, 1);
    bool ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) == kErrnoEPERM);

    // Syscall 582: Settings Set requires SettingsWrite capability
    f.cpu.write_register(16, kSysWiniosSettingsSet);
    f.cpu.write_register(0, f.valid_page_va);
    ok = f.dispatcher.handle_syscall(f.cpu, *f.proc, *f.uas, f.sched);
    TEST_ASSERT(!ok || f.cpu.read_register(0) == kErrnoEPERM);

    std::cout << "  -> Capability enforcement boundary checks passed.\n";
}

int main() {
    std::cout << "=== Stage 231.14: ABI Boundary and Memory Safety Tests ===\n";
    test_settings_boundaries();
    test_multitasking_boundaries();
    test_diag_and_app_boundaries();
    test_capability_enforcement_boundary();
    std::cout << "=== All Stage 231.14 Tests PASSED ===\n";
    return 0;
}
