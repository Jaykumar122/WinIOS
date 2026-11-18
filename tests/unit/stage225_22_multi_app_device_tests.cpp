#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

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
    std::cout << "[Test Stage 225.22] Multi-Application Virtual Device Integration...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL);
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    // Initialize Device Registry
    auto& reg = DeviceRegistry::get();
    reg.reset();
    auto disp_svc = std::make_shared<VirtualDisplayService>();
    auto audio_svc = std::make_shared<VirtualAudioService>();
    auto sensor_svc = std::make_shared<VirtualSensorService>();
    auto net_svc = std::make_shared<VirtualNetworkService>();
    auto clock_svc = std::make_shared<VirtualClockService>();
    auto notif_svc = std::make_shared<VirtualNotificationService>();

    reg.register_device(disp_svc);
    reg.register_device(audio_svc);
    reg.register_device(sensor_svc);
    reg.register_device(net_svc);
    reg.register_device(clock_svc);
    reg.register_device(notif_svc);
    reg.initialize_all();
    reg.start_all();

    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    WindowManager wm;
    SurfaceResourceManager srm;
    dispatcher.set_window_manager(&wm);
    dispatcher.set_surface_resource_manager(&srm);

    auto vfs = std::make_shared<VirtualFileSystem>();
    dispatcher.set_vfs(vfs.get());

    auto cm = std::make_shared<CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());
    DiagnosticsService diag_svc(nullptr, nullptr, &wm, cm.get());
    dispatcher.set_diagnostics_service(&diag_svc);

    // App 1: Device Center (PID 91)
    const uint64_t root_l2_1 = ppm.allocate_page();
    UserAddressSpace uas1(mem, ppm, root_l2_1, "uas_devcenter");
    uas1.set_asid(9);
    uas1.set_owner_pid(91);
    cm->bind_identity(91, "com.winios.devicecenter");
    cm->grant_mask(91, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate |
                       Capability::DeviceEnumerate | Capability::AudioOutput | Capability::SensorAccess);
    auto iso1 = std::make_shared<IsolatedStorage>(vfs, "com.winios.devicecenter");
    GuestStorageApi storage1(iso1, 91);

    auto img1 = MachoUserLoader::load_image(get_embedded_campaign225_device_center(), uas1, ppm, mem);
    TEST_ASSERT(img1.valid);

    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;
    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas1.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }
    const uint64_t arg_va1 = 0x20000000ULL;
    uas1.map_user_page(arg_va1, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args1 = GuestAppContract::CreateDefault(91, img1.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas1.copyin(arg_va1, &args1, sizeof(args1));

    BsdProcess proc1(91, 1, "DeviceCenter");
    proc1.set_address_space(&uas1);

    // App 2: System Monitor (PID 92)
    const uint64_t root_l2_2 = ppm.allocate_page();
    UserAddressSpace uas2(mem, ppm, root_l2_2, "uas_sysmon");
    uas2.set_asid(10);
    uas2.set_owner_pid(92);
    cm->bind_identity(92, "com.winios.systemmonitor");
    cm->grant_mask(92, Capability::DisplayWindow | Capability::LoggingAccess | Capability::DeviceEnumerate |
                       Capability::NetworkAccess | Capability::NotificationPost | Capability::DiagnosticsAccess);

    auto img2 = MachoUserLoader::load_image(get_embedded_campaign225_system_monitor(), uas2, ppm, mem);
    TEST_ASSERT(img2.valid);

    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas2.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }
    const uint64_t arg_va2 = 0x20000000ULL;
    uas2.map_user_page(arg_va2, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args2 = GuestAppContract::CreateDefault(92, img2.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas2.copyin(arg_va2, &args2, sizeof(args2));

    BsdProcess proc2(92, 1, "SystemMonitor");
    proc2.set_address_space(&uas2);

    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    // Execute App 1 to completion
    CPU cpu1;
    setup_cpu(cpu1, root_l2_1);
    cpu1.set_program_counter(img1.entry_point);
    cpu1.set_sp_el0(stack_base + stack_size - 0x10);
    cpu1.write_register(0, arg_va1);

    bool app1_done = false;
    uint32_t exit1 = 0;
    dispatcher.set_storage_api(&storage1);

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 300000; ++i) {
            cpu1.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 91, 1, cpu1.get_program_counter());
            if (cpu1.get_current_el() == 1 && (cpu1.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu1.read_register(16);
                if (num == 1) {
                    app1_done = true;
                    exit1 = static_cast<uint32_t>(cpu1.read_register(0));
                    proc1.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu1, proc1, uas1, sched);
                cpu1.set_current_el(0);
                cpu1.set_program_counter(cpu1.get_elr_el1());
            }
        }
    }
    TEST_ASSERT(app1_done && exit1 == 91);

    // Execute App 2 to completion
    CPU cpu2;
    setup_cpu(cpu2, root_l2_2);
    cpu2.set_program_counter(img2.entry_point);
    cpu2.set_sp_el0(stack_base + stack_size - 0x10);
    cpu2.write_register(0, arg_va2);

    bool app2_done = false;
    uint32_t exit2 = 0;
    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int i = 0; i < 300000; ++i) {
            cpu2.step(mem);
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 92, 1, cpu2.get_program_counter());
            if (cpu2.get_current_el() == 1 && (cpu2.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu2.read_register(16);
                if (num == 1) {
                    app2_done = true;
                    exit2 = static_cast<uint32_t>(cpu2.read_register(0));
                    proc2.set_state(ProcessState::Zombie);
                    break;
                }
                (void)dispatcher.handle_syscall(cpu2, proc2, uas2, sched);
                cpu2.set_current_el(0);
                cpu2.set_program_counter(cpu2.get_elr_el1());
            }
        }
    }
    TEST_ASSERT(app2_done && exit2 == 92);

    // Multi-window verification: both windows exist in WindowManager
    TEST_ASSERT(wm.window_count() == 2);

    std::cout << "[Test Stage 225.22] PASS: Multi-app device integration verified.\n";
    return 0;
}
