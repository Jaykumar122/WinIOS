#include <iostream>
#include <vector>
#include <cstdint>

#include "loader/macho_user_loader.h"
#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "runtime/execution_boundary.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

void test_genuine_arm64_execution() {
    std::cout << "[Test 1] Stage 218.8: Genuine ARM64 guest execution of compiler-generated Mach-O...\n";

    auto& enforcer = ExecutionBoundaryEnforcer::Get();
    enforcer.Reset();

    // 1. Setup virtual platform memory & page manager
    Memory mem(0x800000); // 8 MB
    memory::PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    uint64_t root_l2 = ppm.allocate_page();
    memory::UserAddressSpace uas(mem, ppm, root_l2, "guest_exec_vm");

    // 2. Load compiled Mach-O binary
    auto bin = MachoUserLoader::get_embedded_test_workload();
    TEST_ASSERT(!bin.empty());

    auto loaded = MachoUserLoader::load_image(bin, uas, ppm, mem);
    TEST_ASSERT(loaded.valid);
    TEST_ASSERT(loaded.entry_point != 0);

    // 3. Setup User Stack (16-byte aligned, 64 KB)
    const uint64_t stack_top_va = 0x70000000ULL;
    const uint64_t stack_pages = 4; // 64 KB
    for (uint64_t p = 0; p < stack_pages; ++p) {
        uint64_t page_va = stack_top_va - ((p + 1) * 0x4000);
        uint64_t pa = ppm.allocate_page();
        TEST_ASSERT(pa != 0);
        uas.map_user_page(page_va, pa, memory::PagePermissions::UserReadWrite);
    }
    const uint64_t initial_sp = stack_top_va - 0x10; // 16-byte aligned

    // 4. Setup CPU
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);

    const uint64_t tcr = 28ULL | (0x1ULL << 8u) | (0x1ULL << 10u) | (0x3ULL << 12u) | (0x2ULL << 14u);
    cpu.get_mmu().set_tcr_el1(tcr);
    cpu.get_mmu().set_mair_el1(0x00FFULL);
    cpu.get_mmu().set_ttbr0_el1(uas.root_table_pa());
    cpu.get_mmu().invalidate_tlb_all();
    cpu.set_sctlr_el1(0x00C50838ULL | (1ULL << 0u) | (1ULL << 14u));

    // Entry state
    const uint64_t exit_sentinel_lr = 0x00000000DEAD0000ULL;
    cpu.set_current_el(0); // EL0 userspace
    cpu.set_program_counter(loaded.entry_point);
    cpu.set_sp_el0(initial_sp);
    cpu.write_register(30, exit_sentinel_lr); // Link register = exit sentinel

    // 5. Execute until exit sentinel reached or bounded steps
    const size_t max_steps = 10000;
    size_t steps_executed = 0;

    ExecutionOriginScope origin_scope(ExecutionOrigin::kGuestCpuRetired);

    while (steps_executed < max_steps) {
        uint64_t pc = cpu.get_program_counter();
        if (pc == exit_sentinel_lr || pc == 0) {
            break;
        }

        cpu.step(mem);
        bool ok = enforcer.RecordGuestInstructionRetirement(1);
        TEST_ASSERT(ok);
        steps_executed++;
    }

    std::cout << "[Info] Steps executed: " << steps_executed << "\n";
    std::cout << "[Info] CPU Return register X0: " << cpu.read_register(0) << "\n";
    std::cout << "[Info] Verified instruction retirements: " << enforcer.GetVerifiedGuestInstructionsRetired() << "\n";

    TEST_ASSERT(steps_executed > 10);
    TEST_ASSERT(enforcer.GetVerifiedGuestInstructionsRetired() == steps_executed);
    TEST_ASSERT(enforcer.GetRejectedIncrements() == 0);

    // Verify computed result: factorial(5) + compute_math(10, 8) + test_memory_ops() = 120 + 120 + 100 = 340
    TEST_ASSERT(cpu.read_register(0) == 340);

    std::cout << "[PASS] test_genuine_arm64_execution\n";
}

int main() {
    std::cout << "=== WinIOS Stage 218.8 ARM64 Execution Tests ===\n";
    test_genuine_arm64_execution();
    std::cout << "[ALL PASS] Stage 218.8 ARM64 Execution tests succeeded.\n";
    return 0;
}
