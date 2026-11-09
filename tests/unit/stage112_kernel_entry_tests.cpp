#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "firmware/apple_boot_args.h"
#include "firmware/apple_boot_config_manager.h"
#include "loader/apple_artifact_admission.h"
#include "runtime/apple_kernel_execution_engine.h"

#define KERNEL_ENTRY_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::runtime;
using namespace winios::firmware;
using namespace winios::loader;

namespace {

// Helper to construct a synthetic encrypted IM4P payload
std::vector<std::uint8_t> create_dummy_encrypted_im4p() {
    std::vector<std::uint8_t> buf(128, 0);
    buf[0] = 0x30; // ASN.1 SEQUENCE
    buf[1] = 0x7E;
    // IM4P tag
    buf[6] = 'I'; buf[7] = 'M'; buf[8] = '4'; buf[9] = 'P';
    // krnl type
    buf[12] = 'k'; buf[13] = 'r'; buf[14] = 'n'; buf[15] = 'l';
    // KBAG tag
    buf[20] = 'K'; buf[21] = 'B'; buf[22] = 'A'; buf[23] = 'G';
    return buf;
}

// -----------------------------------------------------------------------------
// Test 1: Mach-O 64-bit Entry Point Resolution & Physical Mapping
// -----------------------------------------------------------------------------
bool test_macho_entry_resolution() {
    std::cout << "[RUN] Test 1: Mach-O 64-bit Entry Point & Physical Mapping ... ";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Engine initialization must succeed");

    // Default synthetic entry point is 0x40000040 (RAM base 0x40000000 + 0x40 header)
    KERNEL_ENTRY_ASSERT(engine.cpu().get_program_counter() == 0x40000040ULL, "PC must point to resolved entry point 0x40000040");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: KASLR Slide Relocation Application
// -----------------------------------------------------------------------------
bool test_kaslr_slide_relocation() {
    std::cout << "[RUN] Test 2: KASLR Slide Relocation Mechanics ... ";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;
    cfg.kaslr_slide     = 0x01000000ULL; // 16 MB KASLR slide

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Engine initialization with KASLR must succeed");

    // Physical entry point is shifted by slide: 0x40000040 + 0x01000000 = 0x41000040
    KERNEL_ENTRY_ASSERT(engine.cpu().get_program_counter() == 0x41000040ULL, "PC must reflect applied KASLR slide");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Apple Boot-Args Register Convention (X0 Staging)
// -----------------------------------------------------------------------------
bool test_boot_args_x0_staging() {
    std::cout << "[RUN] Test 3: Apple Boot-Args Convention (X0 Pointer) ... ";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    // X0 must strictly hold the physical address of struct boot_args
    KERNEL_ENTRY_ASSERT(engine.cpu().read_register(0) == cfg.boot_args_phys, "X0 must hold physical address of struct boot_args");

    // Verify boot_args contents at that memory address
    const std::uint16_t rev = engine.memory().read_u16(cfg.boot_args_phys);
    const std::uint16_t ver = engine.memory().read_u16(cfg.boot_args_phys + 2);
    KERNEL_ENTRY_ASSERT(rev == 2 || rev == 3, "boot_args revision must be 2 or 3");
    KERNEL_ENTRY_ASSERT(ver == 2 || ver == 3, "boot_args version must be 2 or 3");

    // DeviceTree pointer in boot_args
    const std::uint64_t dt_ptr = engine.memory().read_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP));
    KERNEL_ENTRY_ASSERT(dt_ptr == cfg.devicetree_phys, "boot_args.deviceTreeP must match staged DTB address");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Architectural CPU State on Kernel Entry (EL1h, DAIF, CPACR)
// -----------------------------------------------------------------------------
bool test_cpu_architectural_state_on_entry() {
    std::cout << "[RUN] Test 4: Architectural CPU State on Kernel Entry ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    const auto& cpu = engine.cpu();

    // 1. Must be in EL1
    KERNEL_ENTRY_ASSERT(cpu.get_current_el() == 1, "Must enter in Exception Level 1 (EL1)");

    // 2. Must use SP_EL1 (EL1h mode)
    KERNEL_ENTRY_ASSERT(cpu.get_sp_sel() == true, "Must use SP_EL1 (EL1h mode)");
    KERNEL_ENTRY_ASSERT(cpu.get_stack_pointer() == cfg.boot_stack_phys, "Stack pointer must equal configured boot stack top");
    KERNEL_ENTRY_ASSERT(cpu.get_sp_el1() == cfg.boot_stack_phys, "SP_EL1 must match boot stack top");

    // 3. DAIF interrupts must all be masked (D=1, A=1, I=1, F=1 -> 0x0F)
    KERNEL_ENTRY_ASSERT(cpu.get_daif() == 0x0Fu, "DAIF interrupts must all be masked on entry (0x0F)");

    // 4. CPACR_EL1 FP/SIMD access enabled
    KERNEL_ENTRY_ASSERT(cpu.get_cpacr_el1() == 0x00300000u, "CPACR_EL1 FPEN bits [21:20] must be 0b11 (no FP trap)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Instruction Budget Enforcement
// -----------------------------------------------------------------------------
bool test_instruction_budget_enforcement() {
    std::cout << "[RUN] Test 5: Execution Instruction Budget Enforcement ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 4; // Exactly 4 instructions

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.execution_completed, "Execution run should complete");
    KERNEL_ENTRY_ASSERT(ev.instructions_retired == 4, "Retired instructions must exactly equal budget (4)");
    KERNEL_ENTRY_ASSERT(ev.termination_reason == TerminationReason::BudgetReached, "Termination reason must be BudgetReached");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Step-N Fine-Grained Stepping
// -----------------------------------------------------------------------------
bool test_step_n_execution() {
    std::cout << "[RUN] Test 6: Step-N Fine-Grained Stepping & Tracing ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    const std::uint64_t initial_pc = engine.cpu().get_program_counter();

    // Step 1 instruction
    auto ev1 = engine.step_n(1);
    KERNEL_ENTRY_ASSERT(ev1.instructions_retired == 1, "Should retire exactly 1 instruction");
    KERNEL_ENTRY_ASSERT(engine.cpu().get_program_counter() == initial_pc + 4, "PC should advance by 4 bytes");

    // Step next 3 instructions
    auto ev2 = engine.step_n(3);
    KERNEL_ENTRY_ASSERT(ev2.instructions_retired == 4, "Total retired instructions must be 4");
    KERNEL_ENTRY_ASSERT(engine.cpu().get_program_counter() == initial_pc + 16, "PC should advance by 16 bytes");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Infinite Loop Detection (B .)
// -----------------------------------------------------------------------------
bool test_infinite_loop_detection() {
    std::cout << "[RUN] Test 7: Infinite Loop Detection (Branch-to-Self) ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 500;
    cfg.detect_loops = true;
    cfg.loop_threshold = 16;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    // Write B . at entry point
    const std::uint64_t entry_pc = engine.cpu().get_program_counter();
    engine.memory().write_u32(entry_pc, 0x14000000u); // B .

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.termination_reason == TerminationReason::InfiniteLoop, "Must terminate on InfiniteLoop");
    KERNEL_ENTRY_ASSERT(!ev.termination_detail.empty(), "Termination detail must describe branch-to-self");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Low-Power Wait (WFI) Termination
// -----------------------------------------------------------------------------
bool test_wfi_termination() {
    std::cout << "[RUN] Test 8: Low-Power Wait (WFI) Termination ... ";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    // Overwrite instruction at entry point with WFI (0xD503207F)
    const std::uint64_t entry_pc = engine.cpu().get_program_counter();
    engine.memory().write_u32(entry_pc, 0xD503207Fu); // WFI

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.execution_completed, "Execution should complete");
    KERNEL_ENTRY_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Must terminate on Halted_WFI");
    KERNEL_ENTRY_ASSERT(ev.instructions_retired == 1, "Should retire 1 instruction (WFI)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Memory Write Observation & Trace Recording
// -----------------------------------------------------------------------------
bool test_memory_write_observation() {
    std::cout << "[RUN] Test 9: Memory Write Observation & Trace Recording ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    auto ev = engine.run();
    // The synthetic boot kernel stores a signature into memory:
    // STR X0, [SP, #-16]! or similar
    KERNEL_ENTRY_ASSERT(!ev.memory_access_trace.empty(), "Memory write trace must capture guest writes");

    bool found_write = false;
    for (const auto& rec : ev.memory_access_trace) {
        if (rec.is_write) {
            found_write = true;
            KERNEL_ENTRY_ASSERT(rec.size > 0, "Write record size must be non-zero");
            break;
        }
    }
    KERNEL_ENTRY_ASSERT(found_write, "Memory write record must be recorded");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Register Snapshot Fidelity
// -----------------------------------------------------------------------------
bool test_register_snapshot_fidelity() {
    std::cout << "[RUN] Test 10: Register Snapshot Initial vs Final Fidelity ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 10;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.initial_state.pc != 0, "Initial snapshot PC must be recorded");
    KERNEL_ENTRY_ASSERT(ev.final_state.pc != 0, "Final snapshot PC must be recorded");
    KERNEL_ENTRY_ASSERT(ev.final_state.pc != ev.initial_state.pc, "PC must have advanced between initial and final snapshots");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Fault Detection on Illegal / Undefined Instruction
// -----------------------------------------------------------------------------
bool test_fault_detection_undefined_instruction() {
    std::cout << "[RUN] Test 11: Fault Detection on Undefined Instruction ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    KERNEL_ENTRY_ASSERT(engine.initialize(&err), "Initialization must succeed");

    // Overwrite entry PC with an undefined instruction (0x00000000)
    const std::uint64_t entry_pc = engine.cpu().get_program_counter();
    engine.memory().write_u32(entry_pc, 0x00000000u);

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.termination_reason == TerminationReason::UnsupportedInstruction ||
                        ev.termination_reason == TerminationReason::SynchronousException,
                        "Termination reason must reflect instruction fault");
    KERNEL_ENTRY_ASSERT(!ev.exceptions_encountered.empty(), "Exceptions encountered must be recorded");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 12: Genuine Encrypted Kernelcache Admission Rejection
// -----------------------------------------------------------------------------
bool test_genuine_encrypted_kernel_rejection() {
    std::cout << "[RUN] Test 12: Genuine Encrypted Kernelcache Admission Rejection ... ";

    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = false; // Do not force synthetic
    cfg.kernel_data = create_dummy_encrypted_im4p(); // Encrypted IM4P

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    // Initializing with encrypted data should halt safely
    bool init_res = engine.initialize(&err);
    KERNEL_ENTRY_ASSERT(!init_res, "Engine must not proceed to execute encrypted kernelcache");

    auto ev = engine.run();
    KERNEL_ENTRY_ASSERT(ev.kernel_classification == KernelClassification::GenuineEncrypted_Blocked,
                        "Kernel classification must be GenuineEncrypted_Blocked");
    KERNEL_ENTRY_ASSERT(ev.termination_reason == TerminationReason::SetupError,
                        "Termination reason must be SetupError");
    KERNEL_ENTRY_ASSERT(!ev.blocker_reason.empty(), "Blocker reason must describe GID/SEP encryption barrier");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "=================================================================\n" << std::flush;
        std::cout << " Stage 112: Kernel Entry Handoff & Boot Transition Test Suite   \n" << std::flush;
        std::cout << "=================================================================\n" << std::flush;

        int passed = 0;
        int total = 12;

        if (test_macho_entry_resolution()) ++passed; std::cout << std::flush;
        if (test_kaslr_slide_relocation()) ++passed; std::cout << std::flush;
        if (test_boot_args_x0_staging()) ++passed; std::cout << std::flush;
        if (test_cpu_architectural_state_on_entry()) ++passed; std::cout << std::flush;
        if (test_instruction_budget_enforcement()) ++passed; std::cout << std::flush;
        if (test_step_n_execution()) ++passed; std::cout << std::flush;
        if (test_infinite_loop_detection()) ++passed; std::cout << std::flush;
        if (test_wfi_termination()) ++passed; std::cout << std::flush;
        if (test_memory_write_observation()) ++passed; std::cout << std::flush;
        if (test_register_snapshot_fidelity()) ++passed; std::cout << std::flush;
        if (test_fault_detection_undefined_instruction()) ++passed; std::cout << std::flush;
        if (test_genuine_encrypted_kernel_rejection()) ++passed; std::cout << std::flush;

        std::cout << "=================================================================\n";
        std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
        std::cout << "=================================================================\n" << std::flush;

        return (passed == total) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in main(): " << e.what() << std::endl;
        return 2;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in main()" << std::endl;
        return 3;
    }
}
