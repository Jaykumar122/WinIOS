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
#include <string>
#include <vector>

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

static void test_adversarial_settings_schema() {
    SystemSettings out{};

    // 1. Oversized serialized payload rejection
    std::string oversized(SettingsSchema::kMaxSerializedSizeBytes + 512, 'X');
    TEST_ASSERT(!SettingsSchema::deserialize(oversized, out));

    // 2. Corrupt schema validation
    SystemSettings invalid = SettingsSchema::create_default_settings();
    invalid.schema_version = 999; // unsupported future version
    TEST_ASSERT(!SettingsSchema::validate_system_settings(invalid));

    invalid = SettingsSchema::create_default_settings();
    invalid.display.width = 10; // below min (320)
    TEST_ASSERT(!SettingsSchema::validate_system_settings(invalid));

    invalid = SettingsSchema::create_default_settings();
    invalid.audio.master_volume = 200; // above max (100)
    TEST_ASSERT(!SettingsSchema::validate_system_settings(invalid));

    invalid = SettingsSchema::create_default_settings();
    invalid.accessibility.text_scale_percent = 300; // above max (200)
    TEST_ASSERT(!SettingsSchema::validate_system_settings(invalid));

    std::cout << "  -> Adversarial settings schema validation passed.\n";
}

static void test_adversarial_accessibility_boundary() {
    AccessibilityService a11y(nullptr);
    // 1. Empty tree navigation
    TEST_ASSERT(!a11y.focus_next());
    TEST_ASSERT(!a11y.focus_prev());
    TEST_ASSERT(!a11y.activate_focused());
    AccessibilityNode out{};
    TEST_ASSERT(!a11y.get_focused_node(out));
    TEST_ASSERT(a11y.get_focused_id() == 0);

    // 2. Invalid node registration (id=0)
    AccessibilityNode invalid_node{};
    invalid_node.id = 0;
    TEST_ASSERT(!a11y.register_node(invalid_node));

    std::cout << "  -> Adversarial accessibility boundary tests passed.\n";
}

static void test_adversarial_syscalls() {
    CPU cpu;
    Memory mem(0x1000000);
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
    auto* proc = proc_mgr.create_process("bad_actor", 1);
    TEST_ASSERT(proc != nullptr);

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

    // 1. Null buffer pointer to SYS_WINIOS_SETTINGS_GET
    cpu.write_register(16, kSysWiniosSettingsGet);
    cpu.write_register(0, 0); // null pointer
    bool ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    int64_t res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoEINVAL || res == kErrnoEINVAL);

    // 2. Out-of-bounds key to SYS_WINIOS_SETTINGS_SET
    cpu.write_register(16, kSysWiniosSettingsSet);
    cpu.write_register(0, 999); // invalid key
    cpu.write_register(1, 0);
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoEINVAL || res == kErrnoEINVAL);

    // 3. Out-of-bounds value for Volume (key 5, val 250)
    cpu.write_register(16, kSysWiniosSettingsSet);
    cpu.write_register(0, 5);   // Volume
    cpu.write_register(1, 250); // > 100
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoEINVAL || res == kErrnoEINVAL);

    // 4. Invalid theme index (> 2)
    cpu.write_register(16, kSysWiniosThemeSet);
    cpu.write_register(0, 5); // invalid theme
    cpu.write_register(1, 0);
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoEINVAL || res == kErrnoEINVAL);

    // 5. Terminate non-existent PID
    cpu.write_register(16, kSysWiniosTaskTerminate);
    cpu.write_register(0, 8888); // non-existent PID
    cpu.write_register(1, 1);
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoESRCH || res == kErrnoESRCH);

    // 6. Switch to non-existent PID
    cpu.write_register(16, kSysWiniosTaskSwitch);
    cpu.write_register(0, 9999); // non-existent PID
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoESRCH || res == kErrnoESRCH);

    // 7. Invalid text scale (> 200)
    cpu.write_register(16, kSysWiniosAccessibilitySet);
    cpu.write_register(0, 350); // text scale > 200
    cpu.write_register(1, 0);
    ok = dispatcher.handle_syscall(cpu, *proc, uas, sched);
    TEST_ASSERT(!ok);
    res = static_cast<int64_t>(cpu.read_register(0));
    TEST_ASSERT(res == -kErrnoEINVAL || res == kErrnoEINVAL);

    std::cout << "  -> Adversarial syscall tests passed.\n";
}

int main() {
    std::cout << "[Test Stage 228.23] Adversarial Security & Boundary Testing...\n" << std::flush;
    test_adversarial_settings_schema();
    test_adversarial_accessibility_boundary();
    test_adversarial_syscalls();
    std::cout << "All Stage 228.23 Adversarial Tests PASSED.\n" << std::flush;
    return 0;
}
