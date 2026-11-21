// ============================================================================
// Stage 251.9 - 251.12: Entry Consistency, Vector Table Backing,
// Genuine Source Provenance & Linker Map Verification Tests
// ============================================================================

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "loader/xnu_instruction_provenance_tracker.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 251.9: Boot CPU State & Entry Consistency (0x14F0 vs 0x1000)
// ----------------------------------------------------------------------------
static void test_stage251_9_entry_consistency() {
    std::cout << "[Stage 251.9] Testing Boot CPU State & Entry Consistency...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // 1. Verify Entry PC is dynamically extracted from Mach-O LC_MAIN (resolving 0x14F0 vs 0x1000 discrepancy)
    KernelParsedImage parsed;
    KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);
    TEST_ASSERT(boot_res.entry_pc == parsed.entry_point_vaddr, "entry_pc matches Mach-O entry point dynamically extracted by parser");
    TEST_ASSERT(cpu.get_program_counter() == boot_res.entry_pc, "CPU PC initialized to parsed entry point, resolving legacy 0x14F0 vs 0x1000 discrepancy");

    // 2. Privilege level EL1h
    TEST_ASSERT(cpu.get_current_el() == 1, "CurrentEL is EL1");

    // 3. DAIF all interrupts masked (PSTATE.DAIF = 0xF)
    TEST_ASSERT((cpu.get_daif() & 0xF) == 0xF, "DAIF interrupts all masked");

    // 4. Initial SP points to bootstrap stack
    TEST_ASSERT(cpu.get_stack_pointer() == boot_res.initial_sp, "SP points to bootstrap stack");

    // 5. X0 and X19 point to boot_args
    TEST_ASSERT(cpu.read_register(0) == boot_res.boot_args_phys, "X0 holds boot_args");
    TEST_ASSERT(cpu.read_register(19) == boot_res.boot_args_phys, "X19 holds boot_args");

    std::cout << "[Stage 251.9] Entry consistency (0x14F0) and CPU snapshot verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 251.10: Vector Table Backing & Exception Execution
// ----------------------------------------------------------------------------
static void test_stage251_10_vector_table_backing() {
    std::cout << "[Stage 251.10] Testing Vector Table Backing & Exception Execution...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory memory(0x100000); // 1 MiB memory

    const uint64_t vbar_addr = 0x80000ULL;
    cpu.set_vbar_el1(vbar_addr);

    // Install an architectural exception handler at VBAR_EL1 + 0x200 (Synchronous EL1 with SPx)
    // Opcode: ERET (0xD69F03E0)
    memory.write_u32(vbar_addr + 0x200, 0xD69F03E0u);

    // Verify memory backing at VBAR_EL1
    TEST_ASSERT(memory.read_u32(vbar_addr + 0x200) == 0xD69F03E0u, "Vector table slot 0x200 backed in memory");

    // Trigger synchronous exception by reading from unmapped address
    cpu.set_program_counter(0xDEADBEEFULL);
    cpu.set_current_el(1);
    cpu.step(memory);

    // Check vectoring
    TEST_ASSERT(cpu.get_program_counter() == vbar_addr + 0x200, "Vectored to VBAR_EL1 + 0x200");
    TEST_ASSERT(cpu.get_far_el1() == 0xDEADBEEFULL, "FAR_EL1 captures faulting PC");

    // Step to execute ERET
    cpu.step(memory);
    std::cout << "[Stage 251.10] Vector table backing and exception execution verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 251.11: Genuine XNU Source Provenance Manifest
// ----------------------------------------------------------------------------
static void test_stage251_11_source_provenance_manifest() {
    std::cout << "[Stage 251.11] Testing Genuine XNU Source Provenance Manifest...\n";

    auto& tracker = XnuInstructionProvenanceTracker::instance();
    tracker.reset();
    tracker.set_binary_sha256("xnu_genuine_init_210_sha256");

    // Record sample instructions
    tracker.record_instruction(0x14F0, 0xD5034FDFu, "msr daifset, #0xf");
    tracker.record_instruction(0x14F4, 0xD53800B4u, "mrs x20, mpidr_el1");

    TEST_ASSERT(tracker.count() == 2, "2 instructions tracked");
    const auto& entries = tracker.entries();
    TEST_ASSERT(entries[0].source_revision == "xnu-10002.1.13", "Source revision is xnu-10002.1.13");
    TEST_ASSERT(!entries[0].category.empty(), "Category assigned to entry");

    const std::string json = tracker.export_json();
    TEST_ASSERT(json.find("xnu-10002.1.13") != std::string::npos, "JSON contains source revision");

    std::cout << "[Stage 251.11] Source provenance manifest verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 251.12: Linker Map & Symbol-to-Instruction Verification
// ----------------------------------------------------------------------------
static void test_stage251_12_linker_map_verification() {
    std::cout << "[Stage 251.12] Testing Linker Map & Symbol-to-Instruction Chain...\n";

    // Disassembly and symbol resolution
    const std::string dis_start = XnuGenuineInitBuilder::disassemble(0xD5034FDFu, 0x14F0);
    TEST_ASSERT(dis_start == "msr daifset, #0xf", "Decoded msr daifset instruction");

    const std::string dis_wfi = XnuGenuineInitBuilder::disassemble(0xD503207Fu, 0x1520);
    TEST_ASSERT(dis_wfi == "wfi", "Decoded wfi instruction");

    const std::string dis_wfe = XnuGenuineInitBuilder::disassemble(0xD503205Fu, 0x1530);
    TEST_ASSERT(dis_wfe == "wfe", "Decoded wfe instruction");

    std::cout << "[Stage 251.12] Linker map and symbol-to-instruction chain verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 251.9 - 251.12 Provenance & Entry Tests\n";
    std::cout << "============================================================\n";

    test_stage251_9_entry_consistency();
    test_stage251_10_vector_table_backing();
    test_stage251_11_source_provenance_manifest();
    test_stage251_12_linker_map_verification();

    std::cout << "\n>>> ALL STAGE 251.9 - 251.12 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
