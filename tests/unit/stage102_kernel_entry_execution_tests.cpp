#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/apple_kernel_execution_engine.h"
#include "firmware/apple_boot_args.h"

using namespace winios::runtime;
using namespace winios::firmware;

#define STAGE102_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 102 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to create an encrypted IM4P buffer (ASN.1 sequence + IM4P magic)
std::vector<std::uint8_t> create_dummy_encrypted_im4p() {
    std::vector<std::uint8_t> buf(128, 0);
    buf[0] = 0x30; // ASN.1 SEQUENCE
    buf[1] = 0x7E;
    // Embed IM4P tag
    buf[6] = 'I'; buf[7] = 'M'; buf[8] = '4'; buf[9] = 'P';
    // Embed type "krnl"
    buf[12] = 'k'; buf[13] = 'r'; buf[14] = 'n'; buf[15] = 'l';
    // Embed kbag tag
    buf[20] = 'K'; buf[21] = 'B'; buf[22] = 'A'; buf[23] = 'G';
    return buf;
}

} // namespace

// -----------------------------------------------------------------------------
// Test 1: Guest Execution Harness Setup & Boot Contract Initialization
// -----------------------------------------------------------------------------
static bool test_harness_setup_and_boot_contract_handoff() {
    std::cout << "\n[1] test_harness_setup_and_boot_contract_handoff...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024; // 64 MB
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    STAGE102_ASSERT(engine.initialize(&err), ("Engine initialize should succeed: " + err).c_str());

    // Verify CPU architectural state according to Apple Silicon boot contract:
    // EL1h, interrupts masked, FP enabled
    STAGE102_ASSERT(engine.cpu().get_current_el() == 1, "Must enter in EL1 (current_el == 1)");
    STAGE102_ASSERT(engine.cpu().get_sp_sel() == true, "Must use SP_EL1 (sp_sel == true / EL1h)");
    STAGE102_ASSERT(engine.cpu().get_daif() == 0x0Fu, "All interrupts must be masked (DAIF == 0x0F)");
    STAGE102_ASSERT(engine.cpu().get_cpacr_el1() == 0x00300000u, "CPACR_EL1 FP/SIMD must be enabled");

    // Verify Register Handoff:
    // X0 = physical address of struct boot_args
    STAGE102_ASSERT(engine.cpu().read_register(0) == cfg.boot_args_phys, "X0 must point to struct boot_args");
    STAGE102_ASSERT(engine.cpu().get_stack_pointer() == cfg.boot_stack_phys, "SP must equal boot stack top");
    STAGE102_ASSERT(engine.cpu().get_sp_el1() == cfg.boot_stack_phys, "SP_EL1 must equal boot stack top");
    STAGE102_ASSERT(engine.cpu().get_program_counter() == 0x40000040ULL, "PC must point to kernel entry point");

    // Verify struct boot_args staged in RAM
    STAGE102_ASSERT(engine.memory().read_u16(cfg.boot_args_phys) == 2, "boot_args->Revision must be 2");
    STAGE102_ASSERT(engine.memory().read_u16(cfg.boot_args_phys + 2) == 2, "boot_args->Version must be 2");
    STAGE102_ASSERT(engine.memory().read_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP)) == cfg.devicetree_phys, "boot_args->deviceTreeP match");

    std::cout << "[PASS] Harness Setup and Boot Contract Handoff Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Instruction-by-Instruction Stepping and Retirement Tracking
// -----------------------------------------------------------------------------
static bool test_single_step_instruction_retirement() {
    std::cout << "\n[2] test_single_step_instruction_retirement...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine");

    // Step 1: MOV X19, X0
    auto ev1 = engine.step_n(1);
    STAGE102_ASSERT(ev1.instructions_retired == 1, "Must retire exactly 1 instruction");
    STAGE102_ASSERT(ev1.instruction_trace.size() == 1, "Trace must contain 1 record");
    STAGE102_ASSERT(ev1.instruction_trace[0].pc == 0x40000040ULL, "PC of first instruction");
    STAGE102_ASSERT(ev1.instruction_trace[0].instruction_word == 0xAA0003F3u, "Opcode of MOV X19, X0");
    STAGE102_ASSERT(engine.cpu().read_register(19) == cfg.boot_args_phys, "X19 must receive boot_args pointer");
    STAGE102_ASSERT(engine.cpu().get_program_counter() == 0x40000044ULL, "PC must advance 4 bytes");

    // Step 2 more: LDRH W1, [X19, #0] and CMP W1, #2
    auto ev2 = engine.step_n(2);
    STAGE102_ASSERT(ev2.instructions_retired == 3, "Total instructions retired must be 3");
    STAGE102_ASSERT(engine.cpu().read_register(1) == 2, "W1 must receive Revision (2)");
    STAGE102_ASSERT((engine.cpu().get_nzcv() & 0x4u) != 0, "Zero flag (Z) must be set by CMP W1, #2");

    std::cout << "[PASS] Instruction Stepping and Retirement Tracking Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Apple Boot Contract Execution & Success Halt
// -----------------------------------------------------------------------------
static bool test_apple_boot_contract_execution_and_success_halt() {
    std::cout << "\n[3] test_apple_boot_contract_execution_and_success_halt...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine");

    auto ev = engine.run();
    STAGE102_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE102_ASSERT(ev.instructions_retired == 23, "Expected 23 instructions executed in contract sequence");
    STAGE102_ASSERT(ev.final_state.x[0] == 0, "X0 must contain 0 (Success return status)");
    STAGE102_ASSERT(ev.final_state.x[19] == cfg.boot_args_phys, "X19 holds boot_args pointer");
    STAGE102_ASSERT(ev.final_state.x[1] == 2, "X1 holds Revision 2");
    STAGE102_ASSERT(ev.final_state.x[2] == 2, "X2 holds Version 2");
    STAGE102_ASSERT(ev.final_state.x[22] == 0xFFFFFE0007004000ULL, "X22 holds virtBase (BA_VIRT_BASE)");
    STAGE102_ASSERT(ev.final_state.x[23] == cfg.ram_base, "X23 holds physBase (BA_PHYS_BASE)");
    STAGE102_ASSERT(ev.final_state.x[24] == cfg.ram_size, "X24 holds memSize (BA_MEM_SIZE)");
    STAGE102_ASSERT(ev.final_state.x[5] == cfg.devicetree_phys, "X5 holds deviceTreeP");
    STAGE102_ASSERT(ev.final_state.x[6] == 16, "X6 holds deviceTreeLength");
    STAGE102_ASSERT(ev.final_state.x[7] == 0x61647473u, "X7 holds 'adts' magic loaded from DTB");
    STAGE102_ASSERT(ev.final_state.x[26] == kAppleBootFlagDarkBoot, "X26 holds bootFlags (BA_BOOT_FLAGS)");
    STAGE102_ASSERT(ev.final_state.x[27] == cfg.ram_size, "X27 holds memSizeActual");
    STAGE102_ASSERT(ev.final_state.x[20] == 0x504C4150ULL, "X20 holds 'APPL' status token");

    // Verify stack writes:
    // STR X26, [SP, #8] -> bootFlags at SP+8
    // STR W7,  [SP, #0] -> DTB word at SP+0
    const std::uint64_t stack_flags = engine.memory().read_u64(cfg.boot_stack_phys + 8);
    STAGE102_ASSERT(stack_flags == kAppleBootFlagDarkBoot, "Boot stack must contain bootFlags stored by STR X26, [SP, #8]");
    const std::uint32_t stack_val = engine.memory().read_u32(cfg.boot_stack_phys);
    STAGE102_ASSERT(stack_val == 0x61647473u, "Boot stack must contain DTB word stored by STR W7, [SP, #0]");

    // Verify memory access trace recorded the write
    STAGE102_ASSERT(!ev.memory_access_trace.empty(), "Memory access trace must not be empty");
    bool found_stack_write = false;
    for (const auto& m : ev.memory_access_trace) {
        if (m.physical_address == cfg.boot_stack_phys && m.value == 0x61647473u) {
            found_stack_write = true;
            break;
        }
    }
    STAGE102_ASSERT(found_stack_write, "Memory access trace must record stack store at boot_stack_phys");

    std::cout << "[PASS] Apple Boot Contract Full Execution & Success Halt Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Corrupt Boot Args Revision Detection
// -----------------------------------------------------------------------------
static bool test_corrupt_boot_args_revision_error_handling() {
    std::cout << "\n[4] test_corrupt_boot_args_revision_error_handling...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine");

    // Corrupt boot_args->Revision with 0x9999
    engine.memory().write_u16(cfg.boot_args_phys, 0x9999);

    auto ev = engine.run();
    STAGE102_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE102_ASSERT(ev.final_state.x[0] == 1, "X0 must contain 1 (Error status code after branch to error)");

    std::cout << "[PASS] Corrupt Revision Branch-to-Error Correctly Handled\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Null DeviceTree Pointer Detection
// -----------------------------------------------------------------------------
static bool test_null_devicetree_pointer_error_handling() {
    std::cout << "\n[5] test_null_devicetree_pointer_error_handling...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine");

    // Nullify boot_args->deviceTreeP
    engine.memory().write_u64(cfg.boot_args_phys + offsetof(AppleBootArgs, deviceTreeP), 0);

    auto ev = engine.run();
    STAGE102_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE102_ASSERT(ev.final_state.x[0] == 1, "X0 must contain 1 (Error status code after CBZ)");

    std::cout << "[PASS] Null DeviceTree Pointer Error Handled via CBZ\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Execution Budget Enforcement & Infinite Loop Detection
// -----------------------------------------------------------------------------
static bool test_budget_enforcement_and_infinite_loop_detection() {
    std::cout << "\n[6] test_budget_enforcement_and_infinite_loop_detection...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 5;

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine");

    auto ev = engine.run();
    STAGE102_ASSERT(ev.instructions_retired == 5, "Must stop exactly at budget of 5");
    STAGE102_ASSERT(ev.termination_reason == TerminationReason::BudgetReached, "Termination must be BudgetReached");

    // Test loop detection with branch to self: B . (0x14000000)
    cfg.instruction_budget = 1000;
    cfg.detect_loops = true;
    cfg.loop_threshold = 8;
    AppleKernelExecutionEngine loop_engine(cfg);
    STAGE102_ASSERT(loop_engine.initialize(), "Initialize loop engine");

    // Overwrite entry point with 0x14000000 (B .)
    loop_engine.memory().write_u32(0x40000040ULL, 0x14000000u);

    auto loop_ev = loop_engine.run();
    STAGE102_ASSERT(loop_ev.termination_reason == TerminationReason::InfiniteLoop, "Must detect infinite loop");
    STAGE102_ASSERT(loop_ev.instructions_retired >= 8, "Must retire at least loop threshold count");

    std::cout << "[PASS] Execution Budget and Infinite Loop Detection Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: KASLR Slide Relocation and Shifted Entry Point Execution
// -----------------------------------------------------------------------------
static bool test_kaslr_slide_relocation_and_shifted_execution() {
    std::cout << "\n[7] test_kaslr_slide_relocation_and_shifted_execution...\n";

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.force_synthetic = true;
    cfg.kaslr_slide = 0x00040000ULL; // 256 KB KASLR slide (16 KB aligned)

    AppleKernelExecutionEngine engine(cfg);
    STAGE102_ASSERT(engine.initialize(), "Initialize engine with KASLR slide");

    // Slid physical entry point should be 0x40000000 + 0x40000 + 0x40 = 0x40040040
    STAGE102_ASSERT(engine.cpu().get_program_counter() == 0x40040040ULL, "Slid entry point match");

    auto ev = engine.run();
    STAGE102_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Slid kernel must halt at WFI");
    STAGE102_ASSERT(ev.applied_kaslr_slide == 0x00040000ULL, "Applied KASLR slide recorded");
    STAGE102_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0 (Success)");

    std::cout << "[PASS] KASLR Relocation & Shifted Entry Point Execution Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Genuine Encrypted Blocker vs Markdown Evidence Generation
// -----------------------------------------------------------------------------
static bool test_genuine_vs_synthetic_honesty_and_markdown_report() {
    std::cout << "\n[8] test_genuine_vs_synthetic_honesty_and_markdown_report...\n";

    // 1. Candidate Encrypted IM4P Image (Honesty test)
    AppleKernelExecutionConfig enc_cfg;
    enc_cfg.force_synthetic = false;
    enc_cfg.kernel_data = create_dummy_encrypted_im4p();

    AppleKernelExecutionEngine enc_engine(enc_cfg);
    std::string err;
    const bool init_ok = enc_engine.initialize(&err);
    STAGE102_ASSERT(!init_ok, "Encrypted candidate must NOT initialize for raw execution without SEP keys");

    auto enc_ev = enc_engine.run();
    STAGE102_ASSERT(enc_ev.kernel_classification == KernelClassification::GenuineEncrypted_Blocked,
                    "Must classify as GenuineEncrypted_Blocked");
    STAGE102_ASSERT(!enc_ev.blocker_reason.empty(), "Must report honest blocker reason");
    STAGE102_ASSERT(enc_ev.instructions_retired == 0, "Must not fake retired instructions on encrypted kernel");

    // 2. Markdown Evidence Generation
    AppleKernelExecutionConfig synth_cfg;
    synth_cfg.ram_base = 0x40000000ULL;
    synth_cfg.ram_size = 64ULL * 1024 * 1024;
    synth_cfg.boot_stack_phys = 0x43F00000ULL;
    synth_cfg.boot_args_phys  = 0x43F80000ULL;
    synth_cfg.devicetree_phys = 0x43E00000ULL;
    synth_cfg.force_synthetic = true;

    AppleKernelExecutionEngine synth_engine(synth_cfg);
    STAGE102_ASSERT(synth_engine.initialize(), "Init synthetic engine");
    auto ev = synth_engine.run();

    std::string md = AppleKernelExecutionEngine::generate_evidence_markdown(ev);
    STAGE102_ASSERT(!md.empty(), "Markdown report must not be empty");
    STAGE102_ASSERT(md.find("# Stage 102 — Genuine Apple Kernel Entry Execution Evidence") != std::string::npos,
                    "Report title");
    STAGE102_ASSERT(md.find("## 1. Execution Verdict & Classification") != std::string::npos, "Verdict section");
    STAGE102_ASSERT(md.find("## 2. Boot Contract & Register Handoff") != std::string::npos, "Contract section");
    STAGE102_ASSERT(md.find("## 3. Initial vs Final Register Comparison") != std::string::npos, "Register section");
    STAGE102_ASSERT(md.find("## 4. Instruction Retirement Trace") != std::string::npos, "Trace section");
    STAGE102_ASSERT(md.find("## 5. Memory Write Footprint") != std::string::npos, "Memory section");

    std::cout << "[PASS] Genuine Encrypted Blocker & Markdown Report Verified\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "WINIOS Stage 102: Apple Kernel Entry Execution Test Suite\n";
    std::cout << "========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        if (test_fn()) {
            passed++;
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_harness_setup_and_boot_contract_handoff, "test_harness_setup_and_boot_contract_handoff");
    run_test(test_single_step_instruction_retirement, "test_single_step_instruction_retirement");
    run_test(test_apple_boot_contract_execution_and_success_halt, "test_apple_boot_contract_execution_and_success_halt");
    run_test(test_corrupt_boot_args_revision_error_handling, "test_corrupt_boot_args_revision_error_handling");
    run_test(test_null_devicetree_pointer_error_handling, "test_null_devicetree_pointer_error_handling");
    run_test(test_budget_enforcement_and_infinite_loop_detection, "test_budget_enforcement_and_infinite_loop_detection");
    run_test(test_kaslr_slide_relocation_and_shifted_execution, "test_kaslr_slide_relocation_and_shifted_execution");
    run_test(test_genuine_vs_synthetic_honesty_and_markdown_report, "test_genuine_vs_synthetic_honesty_and_markdown_report");

    std::cout << "\n========================================================\n";
    std::cout << "Stage 102 Test Summary: " << passed << " passed, " << failed << " failed\n";
    std::cout << "========================================================\n";

    return (failed == 0) ? 0 : 1;
}
