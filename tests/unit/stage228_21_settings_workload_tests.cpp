#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign228_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/guest_app_contract.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"
#include "runtime/execution_boundary.h"
#include "settings/system_state_manager.h"
#include "settings/control_center_service.h"
#include "settings/task_manager.h"
#include "settings/accessibility_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::kernel;
using namespace winios::memory;
using namespace winios::runtime;
using namespace winios::graphics;
using namespace winios::security;
using namespace winios::vfs;
using namespace winios::settings;

static void setup_cpu(CPU& cpu, uint64_t root_l2_pa) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(3ULL << 20u);
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL | (1ULL << 14u));
    cpu.set_current_el(0);
}

int main() {
    std::cout << "[Test Stage 228.21] Native ARM64 System Settings Guest Workload...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);

    WindowManager wm;
    dispatcher.set_window_manager(&wm);

    SurfaceResourceManager srm;
    dispatcher.set_surface_resource_manager(&srm);

    auto vfs = std::make_shared<VirtualFileSystem>();
    dispatcher.set_vfs(vfs.get());

    auto cm = std::make_shared<CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    auto state_mgr = std::make_shared<SystemStateManager>(vfs);
    auto cc_svc = std::make_shared<ControlCenterService>(state_mgr.get());
    auto task_mgr = std::make_shared<TaskManager>();
    auto a11y_svc = std::make_shared<AccessibilityService>(state_mgr.get());

    dispatcher.set_system_state_manager(state_mgr.get());
    dispatcher.set_control_center_service(cc_svc.get());
    dispatcher.set_task_manager(task_mgr.get());
    dispatcher.set_accessibility_service(a11y_svc.get());

    cm->bind_identity(97, "com.winios.settings");
    cm->grant_mask(97, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate);

    // Setup process for Settings App (PID 97)
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_settings");
    uas.set_asid(17);
    uas.set_owner_pid(97);

    auto img = MachoUserLoader::load_image(get_embedded_campaign228_settings_app(), uas, ppm, mem);
    TEST_ASSERT(img.valid);

    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;
    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }

    const uint64_t arg_va = 0x20000000ULL;
    uas.map_user_page(arg_va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args = GuestAppContract::CreateDefault(97, img.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas.copyin(arg_va, &args, sizeof(args));

    BsdProcess proc(97, 1, "SettingsApp");
    proc.set_address_space(&uas);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    CPU cpu;
    setup_cpu(cpu, root_l2);
    cpu.set_program_counter(img.entry_point);
    cpu.set_sp_el0(stack_base + stack_size - 0x10);
    cpu.write_register(0, arg_va);

    bool app_done = false;
    uint32_t exit_code = 0;

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 400000; ++i) {
            cpu.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 97, 1, cpu.get_program_counter());
            if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu.read_register(16);
                if (num == 1) { // SYS_exit
                    app_done = true;
                    exit_code = static_cast<uint32_t>(cpu.read_register(0));
                    break;
                }
                dispatcher.handle_syscall(cpu, proc, uas, sched);
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }
    }

    TEST_ASSERT(app_done);
    TEST_ASSERT(exit_code == 97);

    // Verify mutations performed by ARM64 guest
    auto app = state_mgr->get_appearance();
    TEST_ASSERT(app.theme == ThemeMode::Light);
    TEST_ASSERT(app.accent == AccentColor::Orange);

    auto aud = state_mgr->get_audio();
    TEST_ASSERT(aud.master_volume == 92);

    auto pwr = state_mgr->get_power();
    TEST_ASSERT(pwr.low_power_mode == true);

    std::cout << "[PASS] Native ARM64 Settings App executed successfully (Exit Code 97).\n" << std::flush;
    return 0;
}
