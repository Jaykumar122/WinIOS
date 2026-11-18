#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"
#include "cpu/cpu.h"
#include "vfs/vfs.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"
#include "security/capability_manager.h"
#include "winios_sdk/winios_sdk.h"

#include <cassert>
#include <iostream>

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

int main() {
    std::cout << "=== Running Stage 229 Capability Isolation Tests ===\n";

    CPU cpu;
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    dispatcher.set_abi_mode(SyscallAbi::DarwinStrict);

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto cm = std::make_shared<security::CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* init_proc = proc_mgr.create_process("launchd", 0);
    (void)init_proc;
    auto* untrusted_proc = proc_mgr.create_process("untrusted_app", 1);
    TEST_ASSERT(untrusted_proc != nullptr);
    TEST_ASSERT(untrusted_proc->pid() > 1);

    // Bind untrusted identity without granting extra capabilities
    cm->bind_identity(untrusted_proc->pid(), "com.untrusted.sample");

    auto state_mgr = std::make_shared<SystemStateManager>(vfs);
    auto cc_svc = std::make_shared<ControlCenterService>(state_mgr.get());
    auto task_mgr = std::make_shared<TaskManager>();
    auto a11y_svc = std::make_shared<AccessibilityService>(state_mgr.get());

    // Register a target task in task manager (e.g. system service PID 100)
    TaskInfo sys_task{}; sys_task.pid = 100; sys_task.name = "sys_service";
    task_mgr->register_task(sys_task);

    TaskInfo self_task{}; self_task.pid = untrusted_proc->pid(); self_task.name = "untrusted";
    task_mgr->register_task(self_task);

    dispatcher.set_system_state_manager(state_mgr.get());
    dispatcher.set_control_center_service(cc_svc.get());
    dispatcher.set_task_manager(task_mgr.get());
    dispatcher.set_accessibility_service(a11y_svc.get());

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");
    untrusted_proc->set_address_space(&uas);

    const std::uint64_t data_page_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, data_page_pa, PagePermissions::UserReadWrite);

    // 1. Untrusted process attempts SYS_WINIOS_SETTINGS_SET -> EPERM
    {
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5); // volume
        cpu.write_register(1, 10);
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);
        std::cout << "  -> Unauthorized SYS_WINIOS_SETTINGS_SET rejected with EPERM.\n";
    }

    // 2. Untrusted process attempts SYS_WINIOS_THEME_SET -> EPERM
    {
        cpu.write_register(16, kSysWiniosThemeSet);
        cpu.write_register(0, 0);
        cpu.write_register(1, 0);
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);
        std::cout << "  -> Unauthorized SYS_WINIOS_THEME_SET rejected with EPERM.\n";
    }

    // 3. Untrusted process attempts SYS_WINIOS_TASK_SWITCH -> EPERM
    {
        cpu.write_register(16, kSysWiniosTaskSwitch);
        cpu.write_register(0, 100); // Target system task
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);
        std::cout << "  -> Unauthorized SYS_WINIOS_TASK_SWITCH rejected with EPERM.\n";
    }

    // 4. Untrusted process attempts SYS_WINIOS_TASK_TERMINATE on another PID -> EPERM
    {
        cpu.write_register(16, kSysWiniosTaskTerminate);
        cpu.write_register(0, 100); // Target system task PID 100
        cpu.write_register(1, 0);   // Force kill
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);
        std::cout << "  -> Cross-process task termination rejected with EPERM.\n";
    }

    // 5. Untrusted process self-termination -> Permitted
    {
        cpu.write_register(16, kSysWiniosTaskTerminate);
        cpu.write_register(0, untrusted_proc->pid()); // Self PID
        cpu.write_register(1, 1);                     // Graceful
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        std::cout << "  -> Self-termination permitted.\n";
    }

    // 6. Untrusted task enumeration: isolated to caller only, system task PID 100 hidden
    {
        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0x30000ULL);
        cpu.write_register(1, 10);
        cpu.write_register(2, 0);
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 1); // Only 1 task returned (self)!

        winios_task_entry_t entry{};
        TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(0x30000ULL), &entry, sizeof(entry)));
        TEST_ASSERT(entry.pid == static_cast<std::uint32_t>(untrusted_proc->pid()));
        std::cout << "  -> Task enumeration properly isolated from cross-process tasks.\n";
    }

    // 7. Dynamic Capability Grant and Revocation
    {
        // Grant SettingsWrite capability
        cm->grant(untrusted_proc->pid(), security::Capability::SettingsWrite);

        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5); // volume
        cpu.write_register(1, 45);
        bool ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(state_mgr->get_audio().master_volume == 45);
        std::cout << "  -> Dynamically granted capability allows SYS_WINIOS_SETTINGS_SET.\n";

        // Revoke SettingsWrite capability
        cm->revoke(untrusted_proc->pid(), security::Capability::SettingsWrite);

        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5);
        cpu.write_register(1, 90);
        ok = dispatcher.handle_syscall(cpu, *untrusted_proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEPERM);
        TEST_ASSERT(state_mgr->get_audio().master_volume == 45); // Unchanged
        std::cout << "  -> Revoked capability immediately enforces rejection.\n";
    }

    std::cout << "All Stage 229 Capability Isolation Tests Passed!\n";
    return 0;
}
