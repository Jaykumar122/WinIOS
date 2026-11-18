#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <cstring>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign225_workloads.inl"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"
#include "memory/user_address_space.h"
#include "kernel/user_syscall_dispatcher.h"
#include "kernel/bsd_process.h"
#include "graphics/window_manager.h"
#include "graphics/display_surface.h"
#include "runtime/guest_app_contract.h"
#include "runtime/guest_storage_api.h"
#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
#include "vfs/vfs.h"
#include "runtime/execution_boundary.h"
#include "services/configuration_service.h"
#include "services/diagnostics_service.h"

#include "devices/device_registry.h"
#include "devices/display_service.h"
#include "devices/input_device_service.h"
#include "devices/audio_service.h"
#include "devices/sensor_service.h"
#include "devices/network_service.h"
#include "devices/clock_service.h"
#include "devices/lifecycle_service.h"
#include "devices/notification_service.h"

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
using namespace winios::storage;
using namespace winios::services;
using namespace winios::vfs;
using namespace winios::devices;

static void setup_cpu(CPU& cpu, uint64_t root_l2_pa) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(3ULL << 20u); // Enable FP/NEON
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL | (1ULL << 14u));
    cpu.set_current_el(0);
}

int main() {
    std::cout << "[Test Stage 225.21] Native ARM64 System Monitor Application Execution...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    // 1. Get cross-compiled ARM64 Mach-O binary
    const auto bin = get_embedded_campaign225_system_monitor();
    TEST_ASSERT(!bin.empty());

    // 2. Setup Address Space for PID 92, ASID 10
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "sysmon_app_uas");
    uas.set_asid(10);
    uas.set_owner_pid(92);

    auto img = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(img.valid);

    // 3. Map Stack (64 KB)
    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;
    const uint64_t stack_top = stack_base + stack_size;
    for (uint64_t va = stack_base; va < stack_top; va += 0x4000) {
        uint64_t pa = ppm.allocate_page();
        uas.map_user_page(va, pa, PagePermissions::UserReadWrite);
    }

    // 4. Map & Populate GuestAppArgBlock
    const uint64_t arg_va = 0x20000000ULL;
    uint64_t arg_pa = ppm.allocate_page();
    uas.map_user_page(arg_va, arg_pa, PagePermissions::UserReadWrite);

    GuestAppArgBlock args = GuestAppContract::CreateDefault(92, img.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    TEST_ASSERT(uas.copyin(arg_va, &args, sizeof(args)));

    // 5. Initialize Virtual Device Registry
    auto& reg = DeviceRegistry::get();
    reg.reset();
    auto disp_svc = std::make_shared<VirtualDisplayService>();
    auto net_svc = std::make_shared<VirtualNetworkService>();
    auto clock_svc = std::make_shared<VirtualClockService>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    reg.register_device(disp_svc);
    reg.register_device(net_svc);
    reg.register_device(clock_svc);
    reg.register_device(notif_svc);
    reg.initialize_all();
    reg.start_all();

    // 6. Setup Host Subsystems & Dispatcher
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    WindowManager wm;
    SurfaceResourceManager srm;
    dispatcher.set_window_manager(&wm);
    dispatcher.set_surface_resource_manager(&srm);

    auto vfs = std::make_shared<VirtualFileSystem>();
    dispatcher.set_vfs(vfs.get());

    auto cm = std::make_shared<CapabilityManager>();
    cm->bind_identity(92, "com.winios.systemmonitor");
    cm->grant_mask(92, Capability::DisplayWindow |
                       Capability::LoggingAccess |
                       Capability::DeviceEnumerate |
                       Capability::NetworkAccess |
                       Capability::NotificationPost |
                       Capability::DiagnosticsAccess);
    dispatcher.set_capability_manager(cm.get());

    DiagnosticsService diag_svc(nullptr, nullptr, &wm, cm.get());
    dispatcher.set_diagnostics_service(&diag_svc);

    BsdProcess proc(92, 1, "SystemMonitor");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    // 7. Setup CPU Registers
    CPU cpu;
    setup_cpu(cpu, root_l2);
    cpu.set_program_counter(img.entry_point);
    cpu.set_sp_el0(stack_top - 0x10);
    cpu.write_register(0, arg_va);
    cpu.write_register(30, 0);

    // 8. Step execution
    bool exited = false;
    uint32_t exit_code = 0;
    uint64_t instructions_executed = 0;

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int step = 0; step < 300000; ++step) {
            cpu.step(mem);
            ++instructions_executed;
            bool rec_ok = enforcer.RecordGuestInstructionRetirementExplicit(1, 92, 1, cpu.get_program_counter());
            TEST_ASSERT(rec_ok);

            if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t sys_num = cpu.read_register(16);
                if (sys_num == 1 /* SYS_exit */) {
                    exited = true;
                    exit_code = static_cast<uint32_t>(cpu.read_register(0));
                    proc.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu, proc, uas, sched);
                cpu.set_current_el(0);
                cpu.set_program_counter(cpu.get_elr_el1());
            }
        }
    }

    std::cout << "[Test] Retired instructions: " << instructions_executed
              << ", Exited: " << (exited ? "true" : "false")
              << ", Exit code: " << exit_code << "\n";

    TEST_ASSERT(exited);
    TEST_ASSERT(exit_code == 92);
    TEST_ASSERT(instructions_executed > 100);

    // Verify window was created in WindowManager
    TEST_ASSERT(wm.window_count() == 1);

    // Verify notification was posted into NotificationService
    TEST_ASSERT(notif_svc->total_posted() >= 1);

    std::cout << "[Test Stage 225.21] PASS: System Monitor genuine ARM64 execution verified.\n";
    return 0;
}
