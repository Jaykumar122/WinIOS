#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <memory>
#include <cstring>

#include "loader/macho_user_loader.h"
#include "loader/embedded_campaign222_workloads.inl"
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
#include "security/capability_manager.h"
#include "storage/isolated_storage.h"
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

static void setup_cpu(CPU& cpu, uint64_t root_l2_pa) {
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_cpacr_el1(3ULL << 20u); // Enable FP/NEON for EL0 and EL1
    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | 0x1ULL | (1ULL << 14u));
    cpu.set_current_el(0);
}

void test_interactive_notes_execution() {
    std::cout << "[Test] Native ARM64 Interactive Notes Application Execution...\n" << std::flush;
    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    Memory mem(0x20000000ULL); // 512 MB
    PhysicalPageManager ppm(mem, 0x1000000ULL, 0x10000000ULL, 0x0800000ULL);
    TEST_ASSERT(ppm.initialize());

    // 1. Get cross-compiled ARM64 Mach-O binary
    const auto bin = get_embedded_campaign222_notes();
    TEST_ASSERT(!bin.empty());

    // 2. Setup Address Space for PID 30, ASID 3
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "notes_app_uas");
    uas.set_asid(3);
    uas.set_owner_pid(30);

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

    GuestAppArgBlock args = GuestAppContract::CreateDefault(30, img.entry_point, stack_base, stack_size, 0x30000000ULL, 0x100000ULL);
    TEST_ASSERT(uas.copyin(arg_va, &args, sizeof(args)));

    // 5. Setup subsystems
    VirtualConsole console;
    UserSyscallDispatcher dispatcher(console);
    WindowManager wm;
    SurfaceResourceManager srm;
    dispatcher.set_window_manager(&wm);
    dispatcher.set_surface_resource_manager(&srm);

    CapabilityManager cm;
    cm.grant_mask(30, Capability::DisplayWindow | Capability::StoragePrivate | Capability::LoggingAccess);
    dispatcher.set_capability_manager(&cm);

    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->initialize_standard_hierarchy();
    auto iso_storage = std::make_shared<storage::IsolatedStorage>(vfs, "com.apple.mobilenotes", 1024 * 1024);
    GuestStorageApi storage_api(iso_storage, 30);
    dispatcher.set_storage_api(&storage_api);

    BsdProcess proc(30, 1, "Notes");
    proc.set_address_space(&uas);
    KernelThreadManager th_mgr;
    PriorityScheduler sched(th_mgr, mem);

    // 6. Setup CPU registers
    CPU cpu;
    setup_cpu(cpu, root_l2);
    cpu.set_program_counter(img.entry_point);
    cpu.set_sp_el0(stack_top - 0x10);
    cpu.write_register(0, arg_va);
    cpu.write_register(30, 0);

    // 7. Step execution
    bool exited = false;
    uint32_t exit_code = 0;
    uint64_t instructions_executed = 0;

    {
        ExecutionOriginScope scope(ExecutionOrigin::kGuestCpuRetired);
        for (int step = 0; step < 200000; ++step) {
            cpu.step(mem);
            ++instructions_executed;
            bool rec_ok = enforcer.RecordGuestInstructionRetirementExplicit(1, 30, 1, cpu.get_program_counter());
            TEST_ASSERT(rec_ok);

            // Check for SVC exception (EL0 -> EL1, EC == 0x15)
            if (cpu.get_current_el() == 1 && (cpu.get_esr_el1() >> 26u) == 0x15u) {
                uint64_t sys_num = cpu.read_register(16);
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

    std::cout << "Interactive Notes executed " << instructions_executed << " instructions.\n";
    std::cout << "Exit status: " << exit_code << ", exited = " << exited << "\n";

    TEST_ASSERT(exited);
    TEST_ASSERT(exit_code == 51); // Distinctive exit code from campaign222_interactive_notes.c!
    TEST_ASSERT(instructions_executed > 100);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() > 100);
    TEST_ASSERT(wm.window_count() == 1);

    // Verify file written to storage
    std::vector<uint8_t> note_readback;
    auto read_res = iso_storage->read("active_note.txt", note_readback);
    TEST_ASSERT(read_res == storage::StorageResult::Success);
    TEST_ASSERT(!note_readback.empty());
    std::string saved_text(reinterpret_cast<const char*>(note_readback.data()), note_readback.size());
    std::cout << "Saved note verified in isolated storage: \"" << saved_text << "\"\n";
    TEST_ASSERT(saved_text.find("Review ARM64 Compositor & Input") != std::string::npos);

    std::cout << "[PASS] Native ARM64 Interactive Notes executed successfully with exit code 51!\n";
}

int main() {
    std::cout << "========================================\n";
    std::cout << "STAGE 222.20: NATIVE ARM64 INTERACTIVE NOTES APP TESTS\n";
    std::cout << "========================================\n";
    test_interactive_notes_execution();
    std::cout << "ALL STAGE 222.20 TESTS PASSED (100%).\n";
    return 0;
}
