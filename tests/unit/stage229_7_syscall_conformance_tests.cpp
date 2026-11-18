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
    std::cout << "=== Running Stage 229 Syscall ABI Conformance Tests ===\n";

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
    auto* proc = proc_mgr.create_process("conformance_app", 1);
    TEST_ASSERT(proc != nullptr);

    // Grant all capabilities for conformance testing
    cm->grant_mask(proc->pid(), 0xFFFFFFFFu);

    auto state_mgr = std::make_shared<SystemStateManager>(vfs);
    auto cc_svc = std::make_shared<ControlCenterService>(state_mgr.get());
    auto task_mgr = std::make_shared<TaskManager>();
    auto a11y_svc = std::make_shared<AccessibilityService>(state_mgr.get());

    dispatcher.set_system_state_manager(state_mgr.get());
    dispatcher.set_control_center_service(cc_svc.get());
    dispatcher.set_task_manager(task_mgr.get());
    dispatcher.set_accessibility_service(a11y_svc.get());

    const std::uint64_t root_l2_pa = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2_pa, "test_uas");
    proc->set_address_space(&uas);

    const std::uint64_t data_page_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, data_page_pa, PagePermissions::UserReadWrite);

    // 1. SYS_WINIOS_SETTINGS_GET error cases
    {
        // Null pointer -> EINVAL
        cpu.write_register(16, kSysWiniosSettingsGet);
        cpu.write_register(0, 0);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Unmapped address -> EFAULT
        cpu.write_register(16, kSysWiniosSettingsGet);
        cpu.write_register(0, 0x99990000ULL);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);
    }

    // 2. SYS_WINIOS_SETTINGS_SET boundary & invalid key tests
    {
        // Unknown key -> EINVAL
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 999);
        cpu.write_register(1, 1);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Volume boundary: 101 -> EINVAL
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5); // Volume
        cpu.write_register(1, 101);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Text scale boundary: 50 -> EINVAL
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 9); // Text scale
        cpu.write_register(1, 50);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Valid boundary: volume 100 -> Success
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5);
        cpu.write_register(1, 100);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(state_mgr->get_audio().master_volume == 100);
    }

    // 3. SYS_WINIOS_THEME_SET invalid enum values
    {
        // Theme mode > 2 -> EINVAL
        cpu.write_register(16, kSysWiniosThemeSet);
        cpu.write_register(0, 10);
        cpu.write_register(1, 0);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Accent > 7 -> EINVAL
        cpu.write_register(16, kSysWiniosThemeSet);
        cpu.write_register(0, 0);
        cpu.write_register(1, 20);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);
    }

    // 4. SYS_WINIOS_CONTROL_CENTER_TOGGLE
    {
        // Invalid action -> EINVAL
        cpu.write_register(16, kSysWiniosControlCenterToggle);
        cpu.write_register(0, 88);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Valid show -> visible = 1
        cpu.write_register(16, kSysWiniosControlCenterToggle);
        cpu.write_register(0, 1); // Show
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 1);
        TEST_ASSERT(cc_svc->is_visible());
    }

    // 5. SYS_WINIOS_TASK_ENUMERATE & buffer truncation
    {
        TaskInfo t1{}; t1.pid = 11; t1.name = "T1"; task_mgr->register_task(t1);
        TaskInfo t2{}; t2.pid = 12; t2.name = "T2"; task_mgr->register_task(t2);
        TaskInfo t3{}; t3.pid = 13; t3.name = "T3"; task_mgr->register_task(t3);

        // Null buffer -> EINVAL
        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0);
        cpu.write_register(1, 10);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Zero count -> EINVAL
        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0x30000ULL);
        cpu.write_register(1, 0);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Buffer truncation: request 2 entries out of 3
        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0x30000ULL);
        cpu.write_register(1, 2); // Max 2 entries
        cpu.write_register(2, 0x30500ULL); // out_count pointer
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 2); // Returned truncated count 2

        uint32_t reported_cnt = 0;
        TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(0x30500ULL), &reported_cnt, sizeof(reported_cnt)));
        TEST_ASSERT(reported_cnt == 2);

        // Fault on invalid count pointer
        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0x30000ULL);
        cpu.write_register(1, 2);
        cpu.write_register(2, 0x99990000ULL); // Unmapped out_count
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEFAULT);
    }

    // 6. SYS_WINIOS_TASK_SWITCH & TERMINATE errors
    {
        // Switch to unknown PID -> ESRCH
        cpu.write_register(16, kSysWiniosTaskSwitch);
        cpu.write_register(0, 99999);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoESRCH);

        // Terminate PID 0 -> EINVAL
        cpu.write_register(16, kSysWiniosTaskTerminate);
        cpu.write_register(0, 0);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Terminate unknown PID -> ESRCH
        cpu.write_register(16, kSysWiniosTaskTerminate);
        cpu.write_register(0, 88888);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoESRCH);
    }

    // 7. SYS_WINIOS_ACCESSIBILITY_SET boundaries
    {
        // Out of range text scale -> EINVAL
        cpu.write_register(16, kSysWiniosAccessibilitySet);
        cpu.write_register(0, 300);
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(!ok);
        TEST_ASSERT(cpu.read_register(0) == kErrnoEINVAL);

        // Valid scale 150 -> Success
        cpu.write_register(16, kSysWiniosAccessibilitySet);
        cpu.write_register(0, 150);
        cpu.write_register(1, 1);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(a11y_svc->get_text_scale_percent() == 150);
        TEST_ASSERT(a11y_svc->is_high_contrast());
    }

    std::cout << "All Stage 229 Syscall ABI Conformance Tests Passed!\n";
    return 0;
}
