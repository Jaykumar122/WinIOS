#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign226_workloads.inl"
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
#include "runtime/guest_storage_api.h"
#include "runtime/system_clipboard.h"
#include "security/capability_manager.h"
#include "vfs/vfs.h"
#include "runtime/execution_boundary.h"

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
    std::cout << "[Test Stage 226.19] Native ARM64 Multi-Window Guest Workload...\n" << std::flush;
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

    SystemClipboard clipboard;
    dispatcher.set_system_clipboard(&clipboard);

    auto vfs = std::make_shared<VirtualFileSystem>();
    dispatcher.set_vfs(vfs.get());

    auto cm = std::make_shared<CapabilityManager>();
    dispatcher.set_capability_manager(cm.get());

    // Setup process for Multi-Window App (PID 93)
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "uas_multiwin");
    uas.set_asid(11);
    uas.set_owner_pid(93);

    cm->bind_identity(93, "com.winios.multiwindow");
    cm->grant_mask(93, Capability::DisplayWindow | Capability::LoggingAccess | Capability::StoragePrivate);

    auto img = MachoUserLoader::load_image(get_embedded_campaign226_multiwindow_app(), uas, ppm, mem);
    TEST_ASSERT(img.valid);

    const uint64_t stack_base = 0x7FFF0000ULL;
    const uint64_t stack_size = 0x10000ULL;
    for (uint64_t va = stack_base; va < stack_base + stack_size; va += 0x4000) {
        uas.map_user_page(va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    }

    const uint64_t arg_va = 0x20000000ULL;
    uas.map_user_page(arg_va, ppm.allocate_page(), PagePermissions::UserReadWrite);
    GuestAppArgBlock args = GuestAppContract::CreateDefault(93, img.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    uas.copyin(arg_va, &args, sizeof(args));

    BsdProcess proc(93, 1, "MultiWindowApp");
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
            (void)enforcer.RecordGuestInstructionRetirementExplicit(1, 93, 1, cpu.get_program_counter());
            if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
                const uint64_t num = cpu.read_register(16);
                if (num == 1) { // SYS_exit
                    app_done = true;
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

    TEST_ASSERT(app_done);
    TEST_ASSERT(exit_code == 93);

    // Verify clipboard content set by app
    std::string clip_out;
    TEST_ASSERT(clipboard.get_text(93, clip_out));
    TEST_ASSERT(clip_out == "Campaign 226 Multi-Window Workspace Active");

    std::cout << "[Test Stage 226.19] PASSED successfully.\n" << std::flush;
    return 0;
}
