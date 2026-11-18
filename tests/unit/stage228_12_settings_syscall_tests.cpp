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
#include <cstring>
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
    std::cout << "[Test Stage 228.12] Category C ABI v2.2 Settings, Multitasking & Accessibility Syscalls (581-590)...\n" << std::flush;

    CPU cpu;
    Memory mem(0x1000000); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);

    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    auto cm = std::make_shared<security::CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    BsdProcessManager proc_mgr;
    proc_mgr.initialize();
    auto* proc = proc_mgr.create_process("settings_app", 1);
    TEST_ASSERT(proc != nullptr);

    cm->bind_identity(proc->pid(), "com.winios.settings");

    // Initialize Services
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

    // Map user data page at 0x30000
    const std::uint64_t data_page_pa = ppm.allocate_page();
    uas.map_user_page(0x30000ULL, data_page_pa, PagePermissions::UserReadWrite);

    // 1. Syscall 581: SYS_WINIOS_SETTINGS_GET
    {
        cpu.write_register(16, kSysWiniosSettingsGet);
        cpu.write_register(0, 0x30000ULL); // buffer
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0); // success

        winios_settings_snapshot_t snap{};
        TEST_ASSERT(uas.copyout(reinterpret_cast<const void*>(0x30000ULL), &snap, sizeof(snap)));
        TEST_ASSERT(snap.display_width == 800);
        TEST_ASSERT(snap.display_height == 600);
        TEST_ASSERT(snap.master_volume == 80);
        std::cout << "  -> 581: SYS_WINIOS_SETTINGS_GET passed.\n";
    }

    // 2. Syscall 582: SYS_WINIOS_SETTINGS_SET (key=5 Volume, val=95)
    {
        cpu.write_register(16, kSysWiniosSettingsSet);
        cpu.write_register(0, 5);  // Master volume
        cpu.write_register(1, 95); // New volume
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(state_mgr->get_audio().master_volume == 95);
        std::cout << "  -> 582: SYS_WINIOS_SETTINGS_SET passed.\n";
    }

    // 3. Syscall 583 & 584: SYS_WINIOS_THEME_GET / SET
    {
        // Set Theme: Light (0), Accent: Orange (4)
        cpu.write_register(16, kSysWiniosThemeSet);
        cpu.write_register(0, 0); // Light
        cpu.write_register(1, 4); // Orange
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(state_mgr->get_appearance().theme == ThemeMode::Light);
        TEST_ASSERT(state_mgr->get_appearance().accent == AccentColor::Orange);

        // Get Theme
        cpu.write_register(16, kSysWiniosThemeGet);
        cpu.write_register(0, 0x30000ULL); // out_theme
        cpu.write_register(1, 0x30004ULL); // out_accent
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);

        uint32_t th = 99, ac = 99;
        (void)uas.copyout(reinterpret_cast<const void*>(0x30000ULL), &th, sizeof(th));
        (void)uas.copyout(reinterpret_cast<const void*>(0x30004ULL), &ac, sizeof(ac));
        TEST_ASSERT(th == 0);
        TEST_ASSERT(ac == 4);
        std::cout << "  -> 583/584: SYS_WINIOS_THEME_GET / SET passed.\n";
    }

    // 4. Syscall 585: SYS_WINIOS_CONTROL_CENTER_TOGGLE
    {
        TEST_ASSERT(!cc_svc->is_visible());
        cpu.write_register(16, kSysWiniosControlCenterToggle);
        cpu.write_register(0, 0); // Toggle
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 1); // now visible
        TEST_ASSERT(cc_svc->is_visible());

        // Hide
        cpu.write_register(16, kSysWiniosControlCenterToggle);
        cpu.write_register(0, 2); // Hide
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(!cc_svc->is_visible());
        std::cout << "  -> 585: SYS_WINIOS_CONTROL_CENTER_TOGGLE passed.\n";
    }

    // 5. Syscall 586: SYS_WINIOS_TASK_ENUMERATE
    {
        TaskInfo t1{};
        t1.pid = 40;
        t1.name = "Daemon";
        t1.bundle_id = "com.winios.daemon";
        t1.state = winios::settings::TaskState::Running;
        t1.is_foreground = true;
        task_mgr->register_task(t1);

        TaskInfo t2{};
        t2.pid = 41;
        t2.name = "Browser";
        t2.bundle_id = "com.winios.browser";
        t2.state = winios::settings::TaskState::Running;
        t2.is_foreground = false;
        task_mgr->register_task(t2);

        cpu.write_register(16, kSysWiniosTaskEnumerate);
        cpu.write_register(0, 0x30000ULL); // out array
        cpu.write_register(1, 10);         // max count
        cpu.write_register(2, 0x30800ULL); // out count
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 2); // 2 tasks returned

        uint32_t reported_cnt = 0;
        (void)uas.copyout(reinterpret_cast<const void*>(0x30800ULL), &reported_cnt, sizeof(reported_cnt));
        TEST_ASSERT(reported_cnt == 2);

        winios_task_entry_t entries[2]{};
        (void)uas.copyout(reinterpret_cast<const void*>(0x30000ULL), entries, sizeof(entries));
        TEST_ASSERT(entries[0].pid == 40);
        TEST_ASSERT(entries[0].is_foreground == 1);
        TEST_ASSERT(entries[1].pid == 41);
        TEST_ASSERT(entries[1].is_foreground == 0);
        std::cout << "  -> 586: SYS_WINIOS_TASK_ENUMERATE passed.\n";
    }

    // 6. Syscall 587: SYS_WINIOS_TASK_SWITCH
    {
        cpu.write_register(16, kSysWiniosTaskSwitch);
        cpu.write_register(0, 41); // target PID
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 41);

        TaskInfo t{};
        (void)task_mgr->get_task(41, t);
        TEST_ASSERT(t.is_foreground);
        std::cout << "  -> 587: SYS_WINIOS_TASK_SWITCH passed.\n";
    }

    // 7. Syscall 588: SYS_WINIOS_TASK_TERMINATE
    {
        cpu.write_register(16, kSysWiniosTaskTerminate);
        cpu.write_register(0, 41); // target PID
        cpu.write_register(1, 1);  // graceful
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);

        TaskInfo t{};
        (void)task_mgr->get_task(41, t);
        TEST_ASSERT(t.state == winios::settings::TaskState::Suspended);
        std::cout << "  -> 588: SYS_WINIOS_TASK_TERMINATE passed.\n";
    }

    // 8. Syscall 589 & 590: SYS_WINIOS_ACCESSIBILITY_GET / SET
    {
        cpu.write_register(16, kSysWiniosAccessibilitySet);
        cpu.write_register(0, 175); // text scale
        cpu.write_register(1, 1);   // high contrast
        bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);
        TEST_ASSERT(a11y_svc->get_text_scale_percent() == 175);
        TEST_ASSERT(a11y_svc->is_high_contrast());

        cpu.write_register(16, kSysWiniosAccessibilityGet);
        cpu.write_register(0, 0x30000ULL);
        ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
        TEST_ASSERT(ok);
        TEST_ASSERT(cpu.read_register(0) == 0);

        winios_accessibility_info_t a_info{};
        (void)uas.copyout(reinterpret_cast<const void*>(0x30000ULL), &a_info, sizeof(a_info));
        TEST_ASSERT(a_info.text_scale_percent == 175);
        TEST_ASSERT(a_info.high_contrast == 1);
        std::cout << "  -> 589/590: SYS_WINIOS_ACCESSIBILITY_GET / SET passed.\n";
    }

    std::cout << "All Stage 228.12 Settings Syscalls PASSED.\n" << std::flush;
    return 0;
}
