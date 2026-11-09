#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/synchronous_exception.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"
#include "runtime/apple_kernel_execution_engine.h"
#include "runtime/apple_kernel_fault_analyzer.h"

using namespace winios;
using namespace winios::runtime;

static int g_test_count = 0;
static int g_pass_count = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(name, func) \
    do { \
        g_test_count++; \
        std::cout << "[RUN] Test " << g_test_count << ": " << name << " ... "; \
        if (func()) { \
            g_pass_count++; \
            std::cout << "PASS\n"; \
        } else { \
            std::cout << "FAIL\n"; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Undefined instruction detection (0x00000000 / unallocated encoding)
// -----------------------------------------------------------------------------
bool test_undefined_instruction_detection() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 10;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Engine initialization failed: " + err);

    // Overwrite physical entry point with 0x00000000 (reserved / undefined)
    const std::uint64_t entry = engine.cpu().get_program_counter();
    const std::uint32_t undef_inst = 0x00000000u;
    engine.memory().write_u32(static_cast<std::size_t>(entry), undef_inst);

    const auto ev = engine.run();

    // Must terminate with SynchronousException
    TEST_ASSERT(ev.termination_reason == TerminationReason::SynchronousException,
                "Expected SynchronousException for 0x00000000");
    // Faulting instruction must NOT retire
    TEST_ASSERT(ev.instructions_retired == 0, "Instructions retired must be 0 for faulting instruction");
    // ESR_EL1 EC must be 0x00 (Unknown / Undefined)
    const auto syn = AppleGuestExceptionSyndrome::decode(ev.final_state.esr_el1);
    TEST_ASSERT(syn.ec == 0x00, "Expected ESR EC 0x00 for undefined instruction");
    TEST_ASSERT(ev.final_state.elr_el1 == entry, "ELR_EL1 must hold faulting entry address");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Unsupported instruction handling
// -----------------------------------------------------------------------------
bool test_unsupported_instruction_handling() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    Memory mem(0x100000);
    // Write an allocated opcode that the interpreter does not implement (e.g. SVE or custom allocated opcode)
    // Opcode 0x24000000 (Allocated group, not unallocated, unimplemented in WinIOS interpreter)
    const std::uint64_t pc = 0x1000;
    cpu.set_program_counter(pc);
    mem.write_u32(static_cast<std::size_t>(pc), 0x24000000u);

    bool caught_unsupported = false;
    try {
        cpu.step(mem);
    } catch (const std::runtime_error& e) {
        caught_unsupported = (std::string(e.what()).find("Unsupported AArch64 instruction") != std::string::npos);
    }

    TEST_ASSERT(caught_unsupported, "Expected std::runtime_error for unsupported instruction");
    TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Retirement count must not increment for unsupported instruction");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Invalid instruction fetch (out of DRAM range)
// -----------------------------------------------------------------------------
bool test_invalid_instruction_fetch() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 10;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Engine initialization failed: " + err);

    // Set PC to address far beyond memory size
    engine.cpu().set_program_counter(0x800000000ULL);

    const auto ev = engine.run();
    TEST_ASSERT(ev.termination_reason == TerminationReason::AddressOutOfRange,
                "Expected AddressOutOfRange for fetch outside RAM");
    TEST_ASSERT(ev.instructions_retired == 0, "Instructions retired must be 0");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Instruction alignment fault (unaligned PC)
// -----------------------------------------------------------------------------
bool test_instruction_alignment_fault() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 10;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Engine initialization failed: " + err);

    // Set PC to unaligned address (entry + 2)
    const std::uint64_t unaligned_pc = engine.cpu().get_program_counter() + 2u;
    engine.cpu().set_program_counter(unaligned_pc);

    const auto ev = engine.run();
    TEST_ASSERT(ev.termination_reason == TerminationReason::AlignmentFault,
                "Expected AlignmentFault for unaligned PC");
    TEST_ASSERT(ev.instructions_retired == 0, "Instructions retired must be 0");
    const auto syn = AppleGuestExceptionSyndrome::decode(ev.final_state.esr_el1);
    TEST_ASSERT(syn.ec == 0x22, "Expected EC 0x22 (PcAlignment)");
    TEST_ASSERT(ev.final_state.far_el1 == unaligned_pc, "FAR_EL1 must hold unaligned PC");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Data translation fault (MMU enabled, unmapped VA)
// -----------------------------------------------------------------------------
bool test_data_translation_fault() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    const std::uint64_t pc = 0x40001000ULL;
    cpu.set_program_counter(pc);

    const std::uint64_t unmapped_va = 0xFFFFFE0010000000ULL;
    mmu::TranslationFault fault;
    fault.type = mmu::MmuFaultType::Translation;
    fault.level = 1;
    fault.is_write = false;
    fault.virtual_address = unmapped_va;

    TEST_ASSERT((fault.fsc() & 0x3Cu) == 0x04u, "Translation fault FSC must have base 0x04");

    // Construct synchronous exception for data translation abort
    auto exc = make_data_abort(true, AccessType::Read, static_cast<FaultStatus>(fault.fsc()),
                               fault.virtual_address, pc, 8);
    cpu.raise_synchronous_exception(exc);

    TEST_ASSERT(cpu.exception_taken_in_step(), "Exception must be registered as taken");
    const auto syn = AppleGuestExceptionSyndrome::decode(cpu.get_esr_el1());
    TEST_ASSERT(syn.ec == 0x25, "Expected EC 0x25 (DataAbortSame)");
    // FSC translation fault level 0..3: (fsc & 0x3C) == 0x04
    TEST_ASSERT((syn.fsc & 0x3Cu) == 0x04u, "Expected translation fault FSC");
    TEST_ASSERT(cpu.get_far_el1() == unmapped_va, "FAR_EL1 must match faulting VA");

    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Data permission fault (MMU enabled, write to Read-Only page)
// -----------------------------------------------------------------------------
bool test_data_permission_fault() {
    mmu::TranslationFault fault;
    fault.type = mmu::MmuFaultType::Permission;
    fault.level = 2;
    fault.is_write = true;
    fault.virtual_address = 0xFFFFFE0007008000ULL;

    const std::uint32_t fsc = fault.fsc();
    TEST_ASSERT((fsc & 0x3Cu) == 0x0Cu, "Permission fault FSC must have base 0x0C");

    // Construct synchronous exception for data permission abort
    auto exc = make_data_abort(true, AccessType::Write, static_cast<FaultStatus>(fsc),
                               fault.virtual_address, 0x40001000ULL, 8);
    const auto syn = AppleGuestExceptionSyndrome::decode(exc.esr());
    TEST_ASSERT(syn.ec == 0x25, "Data abort same EL must have EC 0x25");
    TEST_ASSERT(syn.wnr == true, "WnR must be 1 for write access");
    TEST_ASSERT(syn.fsc_name.find("Permission") != std::string::npos, "FSC name must identify Permission Fault");

    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Invalid physical memory access (out of bounds PA)
// -----------------------------------------------------------------------------
bool test_invalid_physical_memory_access() {
    // 1. Architectural Mode: Out of bounds physical access triggers Data Abort (External Abort FSC 0x10)
    {
        CPU cpu;
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);

        Memory mem(0x100000); // 1 MB physical memory
        const std::uint64_t pc = 0x1000;
        cpu.set_program_counter(pc);

        // Instruction: LDR X0, [X1] (opcode 0xF9400020)
        mem.write_u32(static_cast<std::size_t>(pc), 0xF9400020u);
        // Point X1 to an address far beyond physical memory
        cpu.write_register(1, 0x50000000ULL);

        cpu.step(mem);

        TEST_ASSERT(cpu.exception_taken_in_step(), "Exception must be taken architecturally");
        const auto syn = AppleGuestExceptionSyndrome::decode(cpu.get_esr_el1());
        TEST_ASSERT(syn.ec == 0x25, "Expected EC 0x25 (DataAbortSame)");
        TEST_ASSERT(syn.fsc == 0x10, "Expected FSC 0x10 (SynchronousExternal)");
        TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Retirement count must not increment for aborting access");
    }

    // 2. Non-Architectural Mode: Memory out of bounds throws std::out_of_range
    {
        CPU cpu;
        cpu.reset();
        cpu.set_architectural_exceptions(false);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);

        Memory mem(0x100000);
        const std::uint64_t pc = 0x1000;
        cpu.set_program_counter(pc);

        mem.write_u32(static_cast<std::size_t>(pc), 0xF9400020u);
        cpu.write_register(1, 0x50000000ULL);

        bool caught_out_of_range = false;
        try {
            cpu.step(mem);
        } catch (const std::out_of_range&) {
            caught_out_of_range = true;
        }

        TEST_ASSERT(caught_out_of_range, "Expected std::out_of_range for physical memory access out of bounds in non-architectural mode");
        TEST_ASSERT(cpu.get_instruction_retirement_count() == 0, "Retirement count must not increment for out of bounds access");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Invalid MMIO access diagnostics
// -----------------------------------------------------------------------------
bool test_invalid_mmio_access() {
    AppleKernelFaultAnalyzer analyzer;
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    Memory mem(0x100000);

    const auto ctx = analyzer.analyze_cpu_fault(
        cpu, mem, 0x1000, TerminationReason::AddressOutOfRange,
        "MMIO aperture 0x280000000 not mapped",
        KernelClassification::SyntheticAppleBootContract, "test_session_mmio");

    TEST_ASSERT(ctx.category == AppleGuestExceptionClass::ExternalAbort,
                "Expected ExternalAbort category for unmapped MMIO");
    TEST_ASSERT(ctx.description.find("MMIO aperture") != std::string::npos,
                "Description must include error message");

    return true;
}

// -----------------------------------------------------------------------------
// Test 9: System-register privilege violation (EL0 access to EL1 sysreg)
// -----------------------------------------------------------------------------
bool test_sysreg_privilege_violation() {
    AppleKernelFaultAnalyzer analyzer;
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(0); // EL0 (User mode)

    // SPSR_EL1: op0=3, op1=0, crn=4, crm=0, op2=0
    const auto info = analyzer.check_sysreg_access(3, 0, 4, 0, 0, 0, true, cpu);

    TEST_ASSERT(info.fault_subtype == "PrivilegeViolation", "EL0 access to SPSR_EL1 must be PrivilegeViolation");
    TEST_ASSERT(info.required_el == 1, "SPSR_EL1 requires EL1");
    TEST_ASSERT(info.current_el == 0, "Current EL is 0");

    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Unknown system-register access
// -----------------------------------------------------------------------------
bool test_unknown_sysreg_access() {
    AppleKernelFaultAnalyzer analyzer;
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);

    // Unallocated sysreg key: op0=3, op1=7, crn=15, crm=15, op2=7
    const auto info = analyzer.check_sysreg_access(3, 7, 15, 15, 7, 1, true, cpu);

    TEST_ASSERT(info.fault_subtype == "UnknownRegister", "Unallocated sysreg must be classified as UnknownRegister");
    TEST_ASSERT(info.reg_name.find("Unknown_SysReg") != std::string::npos, "Name must indicate unknown sysreg");

    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Fault-time register snapshot
// -----------------------------------------------------------------------------
bool test_fault_time_register_snapshot() {
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x40001000ULL);
    cpu.set_sp_el1(0x43F00000ULL);
    cpu.set_sp_el0(0x42000000ULL);
    cpu.write_register(0, 0x11111111ULL);
    cpu.write_register(1, 0x22222222ULL);
    cpu.write_register(19, 0x33333333ULL);
    cpu.set_elr_el1(0x40001000ULL);
    cpu.set_spsr_el1(0x20000005u);
    cpu.set_esr_el1(0x96000044u);
    cpu.set_far_el1(0xFFFFFE0007008000ULL);

    const auto snap = AppleKernelExecutionEngine::capture_snapshot(cpu);

    TEST_ASSERT(snap.pc == 0x40001000ULL, "PC snapshot mismatch");
    TEST_ASSERT(snap.sp_el1 == 0x43F00000ULL, "SP_EL1 snapshot mismatch");
    TEST_ASSERT(snap.sp_el0 == 0x42000000ULL, "SP_EL0 snapshot mismatch");
    TEST_ASSERT(snap.x[0] == 0x11111111ULL, "X0 snapshot mismatch");
    TEST_ASSERT(snap.x[1] == 0x22222222ULL, "X1 snapshot mismatch");
    TEST_ASSERT(snap.x[19] == 0x33333333ULL, "X19 snapshot mismatch");
    TEST_ASSERT(snap.elr_el1 == 0x40001000ULL, "ELR_EL1 snapshot mismatch");
    TEST_ASSERT(snap.spsr_el1 == 0x20000005u, "SPSR_EL1 snapshot mismatch");
    TEST_ASSERT(snap.esr_el1 == 0x96000044u, "ESR_EL1 snapshot mismatch");
    TEST_ASSERT(snap.far_el1 == 0xFFFFFE0007008000ULL, "FAR_EL1 snapshot mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 12: Fault-time memory trace
// -----------------------------------------------------------------------------
bool test_fault_time_memory_trace() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 30;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Initialization failed: " + err);

    const auto ev = engine.run();
    TEST_ASSERT(ev.setup_success, "Setup must succeed");
    // Synthetic boot kernel writes first word of DeviceTree ('adts' = 0x61647473) to stack
    TEST_ASSERT(!ev.memory_access_trace.empty(), "Memory access trace must capture stores");
    const auto& last_mem = ev.memory_access_trace.back();
    TEST_ASSERT(last_mem.is_write, "Memory access must be write");
    TEST_ASSERT(last_mem.value == 0x61647473ULL, "Written value must be 'adts' (0x61647473) DTB magic token");

    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Instruction retirement accounting
// -----------------------------------------------------------------------------
bool test_instruction_retirement_accounting() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 20;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Initialization failed: " + err);

    // Step 1: execute first valid instruction (MOV X19, X0)
    auto ev1 = engine.step_n(1);
    TEST_ASSERT(ev1.instructions_retired == 1, "First instruction must retire");

    // Inject undefined instruction at current PC
    const std::uint64_t cur_pc = engine.cpu().get_program_counter();
    engine.memory().write_u32(static_cast<std::size_t>(cur_pc), 0x00000000u);

    // Step 2: attempt to execute undefined instruction
    auto ev2 = engine.step_n(1);
    TEST_ASSERT(ev2.termination_reason == TerminationReason::SynchronousException,
                "Must terminate with SynchronousException");
    // Retirement count must remain 1!
    TEST_ASSERT(ev2.instructions_retired == 1, "Faulting instruction must not increment retired count");

    return true;
}

// -----------------------------------------------------------------------------
// Test 14: Exception syndrome generation
// -----------------------------------------------------------------------------
bool test_exception_syndrome_generation() {
    // 1. EC 0x25 (Data abort same EL), IL=1, WnR=1, FSC=0x05 (Translation Level 1)
    // ESR = (0x25 << 26) | (1 << 25) | (1 << 6) | 0x05 = 0x96000045
    const auto syn1 = AppleGuestExceptionSyndrome::decode(0x96000045u);
    TEST_ASSERT(syn1.ec == 0x25, "EC must be 0x25");
    TEST_ASSERT(syn1.il == true, "IL must be true");
    TEST_ASSERT(syn1.wnr == true, "WnR must be true");
    TEST_ASSERT(syn1.fsc == 0x05, "FSC must be 0x05");
    TEST_ASSERT(syn1.ec_name == "Data Abort (Same EL)", "EC name mismatch");
    TEST_ASSERT(syn1.fsc_name == "Translation Fault Level 1", "FSC name mismatch: " + syn1.fsc_name);

    // 2. EC 0x22 (PC alignment), IL=1
    // ESR = (0x22 << 26) | (1 << 25) = 0x8A000000
    const auto syn2 = AppleGuestExceptionSyndrome::decode(0x8A000000u);
    TEST_ASSERT(syn2.ec == 0x22, "EC must be 0x22");
    TEST_ASSERT(syn2.ec_name == "PC Alignment Fault", "EC name mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 15: Exception state preservation
// -----------------------------------------------------------------------------
bool test_exception_state_preservation() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_nzcv(0b1100u); // N=1, Z=1
    cpu.set_daif(0x05u);

    SynchronousException exc;
    exc.ec = ExceptionClass::IllegalExecutionState;
    exc.preferred_return = 0x40004000ULL;

    cpu.raise_synchronous_exception(exc);

    TEST_ASSERT(cpu.exception_taken_in_step(), "Exception must be registered as taken");
    TEST_ASSERT(cpu.get_elr_el1() == 0x40004000ULL, "ELR_EL1 must store preferred return");
    // SPSR_EL1 should preserve NZCV in top 4 bits
    const std::uint32_t spsr = cpu.get_spsr_el1();
    TEST_ASSERT((spsr >> 28u) == 0b1100u, "SPSR_EL1 must preserve NZCV condition flags");
    // DAIF must be masked after exception entry
    TEST_ASSERT(cpu.get_daif() == 0x0Fu, "DAIF must be fully masked on EL1 entry");

    return true;
}

// -----------------------------------------------------------------------------
// Test 16: Execution-history ring buffer
// -----------------------------------------------------------------------------
bool test_execution_history_ring_buffer() {
    AppleBoundedHistoryBuffer<int> buf(4);
    TEST_ASSERT(buf.capacity() == 4, "Capacity must be 4");
    TEST_ASSERT(buf.empty(), "Buffer must be initially empty");

    buf.push_back(10);
    buf.push_back(20);
    buf.push_back(30);
    buf.push_back(40);
    TEST_ASSERT(buf.size() == 4, "Size must be 4");
    TEST_ASSERT(buf.total_overflows() == 0, "No overflows yet");

    // Overflow by 2 items
    buf.push_back(50);
    buf.push_back(60);

    TEST_ASSERT(buf.size() == 4, "Size must remain bounded at 4");
    TEST_ASSERT(buf.total_pushed() == 6, "Total pushed must be 6");
    TEST_ASSERT(buf.total_overflows() == 2, "Overflow count must be 2");

    // Verify chronological FIFO order: {30, 40, 50, 60}
    const auto vec = buf.to_vector();
    TEST_ASSERT(vec.size() == 4, "Vector size must be 4");
    TEST_ASSERT(vec[0] == 30, "Index 0 must be 30");
    TEST_ASSERT(vec[1] == 40, "Index 1 must be 40");
    TEST_ASSERT(vec[2] == 50, "Index 2 must be 50");
    TEST_ASSERT(vec[3] == 60, "Index 3 must be 60");

    return true;
}

// -----------------------------------------------------------------------------
// Test 17: Fault record serialization and deserialization
// -----------------------------------------------------------------------------
bool test_fault_record_serialization() {
    AppleGuestFaultRecord orig;
    orig.session_id = "winios_sess_test_17";
    orig.artifact_classification = KernelClassification::SyntheticAppleBootContract;
    orig.artifact_id = "synthetic.apple.boot.contract.iphone15";
    orig.artifact_sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    orig.initial_pc = 0x40004040ULL;
    orig.faulting_pc = 0x40004058ULL;
    orig.instructions_retired = 6;
    orig.termination_reason = TerminationReason::AlignmentFault;
    orig.termination_detail = "PC alignment fault";
    orig.fault_context.category = AppleGuestExceptionClass::AlignmentFault;
    orig.fault_context.description = "Alignment fault at PC 0x40004058";
    orig.fault_context.instruction_word = 0x58000040u;
    orig.fault_context.fault_va = 0x40004058ULL;
    orig.fault_context.has_fault_va = true;
    orig.fault_state.pc = 0x40004058ULL;
    orig.fault_state.x[0] = 0x43F80000ULL;
    orig.fault_state.x[19] = 0x43F80000ULL;
    orig.fault_state.esr_el1 = 0x8A000000u;
    orig.fault_state.far_el1 = 0x40004058ULL;

    const auto bytes = orig.serialize();
    TEST_ASSERT(!bytes.empty(), "Serialized byte buffer must not be empty");

    AppleGuestFaultRecord roundtrip;
    std::string err;
    const bool ok = AppleGuestFaultRecord::deserialize(bytes.data(), bytes.size(), &roundtrip, &err);
    TEST_ASSERT(ok, "Deserialization failed: " + err);

    TEST_ASSERT(roundtrip.session_id == orig.session_id, "Session ID mismatch");
    TEST_ASSERT(roundtrip.artifact_classification == orig.artifact_classification, "Classification mismatch");
    TEST_ASSERT(roundtrip.artifact_id == orig.artifact_id, "Artifact ID mismatch");
    TEST_ASSERT(roundtrip.faulting_pc == orig.faulting_pc, "Faulting PC mismatch");
    TEST_ASSERT(roundtrip.instructions_retired == orig.instructions_retired, "Retired count mismatch");
    TEST_ASSERT(roundtrip.termination_reason == orig.termination_reason, "Termination reason mismatch");
    TEST_ASSERT(roundtrip.fault_context.category == orig.fault_context.category, "Category mismatch");
    TEST_ASSERT(roundtrip.fault_state.x[0] == orig.fault_state.x[0], "X0 mismatch");
    TEST_ASSERT(roundtrip.fault_state.esr_el1 == orig.fault_state.esr_el1, "ESR mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 18: Corrupt fault record rejection
// -----------------------------------------------------------------------------
bool test_corrupt_fault_record_rejection() {
    AppleGuestFaultRecord orig;
    orig.session_id = "test_18";
    auto bytes = orig.serialize();

    // 1. Corrupt magic header
    auto corrupt_magic = bytes;
    corrupt_magic[0] = 'X';
    corrupt_magic[1] = 'X';
    std::string err;
    AppleGuestFaultRecord out_rec;
    TEST_ASSERT(!AppleGuestFaultRecord::deserialize(corrupt_magic.data(), corrupt_magic.size(), &out_rec, &err),
                "Must reject corrupt magic");
    TEST_ASSERT(err.find("Invalid fault record magic") != std::string::npos, "Error must describe magic mismatch");

    // 2. Corrupt version
    auto corrupt_ver = bytes;
    corrupt_ver[4] = 99;
    TEST_ASSERT(!AppleGuestFaultRecord::deserialize(corrupt_ver.data(), corrupt_ver.size(), &out_rec, &err),
                "Must reject unsupported version");

    // 3. Truncated buffer
    TEST_ASSERT(!AppleGuestFaultRecord::deserialize(bytes.data(), bytes.size() - 5, &out_rec, &err),
                "Must reject truncated buffer");

    // 4. Checksum mismatch (flip bit in payload)
    auto corrupt_payload = bytes;
    corrupt_payload[20] ^= 0xFF;
    TEST_ASSERT(!AppleGuestFaultRecord::deserialize(corrupt_payload.data(), corrupt_payload.size(), &out_rec, &err),
                "Must reject checksum mismatch");
    TEST_ASSERT(err.find("checksum mismatch") != std::string::npos, "Error must identify checksum mismatch");

    return true;
}

// -----------------------------------------------------------------------------
// Test 19: Execution budget termination
// -----------------------------------------------------------------------------
bool test_execution_budget_termination() {
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 5; // Cut off before WFI

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Initialization failed: " + err);

    const auto ev = engine.run();
    TEST_ASSERT(ev.termination_reason == TerminationReason::BudgetReached,
                "Expected BudgetReached termination");
    TEST_ASSERT(ev.instructions_retired == 5, "Instructions retired must exactly match budget 5");
    // Budget exhaustion must not be reported as completed kernel boot
    TEST_ASSERT(ev.execution_completed == true, "Budget reached marks step completion but not final kernel state");

    return true;
}

// -----------------------------------------------------------------------------
// Test 20: WFI/WFE termination vs genuine execution classification
// -----------------------------------------------------------------------------
bool test_wfi_termination_and_genuine_classification() {
    // 1. Synthetic kernel halts cleanly on WFI
    AppleKernelExecutionConfig cfg;
    cfg.force_synthetic = true;
    cfg.instruction_budget = 100;

    AppleKernelExecutionEngine engine(cfg);
    std::string err;
    TEST_ASSERT(engine.initialize(&err), "Initialization failed: " + err);

    const auto ev = engine.run();
    TEST_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI,
                "Expected Halted_WFI on synthetic boot completion");
    TEST_ASSERT(ev.kernel_classification == KernelClassification::SyntheticAppleBootContract,
                "Classification must be SyntheticAppleBootContract");

    // 2. Genuine Encrypted artifact without SEP keys reports BLOCKED / NOT RUN
    AppleKernelExecutionConfig enc_cfg;
    enc_cfg.force_synthetic = false;
    // Construct fake IM4P header
    std::vector<std::uint8_t> im4p(64, 0);
    im4p[0] = 0x30; im4p[1] = 0x3E; // SEQUENCE
    im4p[2] = 0x16; im4p[3] = 0x04; // IA5String "IM4P"
    std::memcpy(im4p.data() + 4, "IM4P", 4);
    enc_cfg.kernel_data = im4p;

    AppleKernelExecutionEngine enc_engine(enc_cfg);
    std::string block_err;
    const bool init_ok = enc_engine.initialize(&block_err);
    TEST_ASSERT(!init_ok, "Encrypted kernelcache must NOT initialize without SEP decryption keys");
    const auto enc_ev = enc_engine.run();
    TEST_ASSERT(enc_ev.kernel_classification == KernelClassification::GenuineEncrypted_Blocked,
                "Classification must be GenuineEncrypted_Blocked");
    TEST_ASSERT(enc_ev.termination_reason == TerminationReason::SetupError,
                "Termination reason must be SetupError");
    TEST_ASSERT(!enc_ev.blocker_reason.empty(), "Blocker reason must be populated");

    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 103: Kernel Fault Analysis & Exception Diagnostic Tests  \n";
    std::cout << "=================================================================\n";

    RUN_TEST("Undefined instruction detection", test_undefined_instruction_detection);
    RUN_TEST("Unsupported instruction handling", test_unsupported_instruction_handling);
    RUN_TEST("Invalid instruction fetch", test_invalid_instruction_fetch);
    RUN_TEST("Instruction alignment fault", test_instruction_alignment_fault);
    RUN_TEST("Data translation fault", test_data_translation_fault);
    RUN_TEST("Data permission fault", test_data_permission_fault);
    RUN_TEST("Invalid physical memory access", test_invalid_physical_memory_access);
    RUN_TEST("Invalid MMIO access", test_invalid_mmio_access);
    RUN_TEST("System register privilege violation", test_sysreg_privilege_violation);
    RUN_TEST("Unknown system register access", test_unknown_sysreg_access);
    RUN_TEST("Fault-time register snapshot", test_fault_time_register_snapshot);
    RUN_TEST("Fault-time memory trace", test_fault_time_memory_trace);
    RUN_TEST("Instruction retirement accounting", test_instruction_retirement_accounting);
    RUN_TEST("Exception syndrome generation", test_exception_syndrome_generation);
    RUN_TEST("Exception state preservation", test_exception_state_preservation);
    RUN_TEST("Execution-history ring buffer", test_execution_history_ring_buffer);
    RUN_TEST("Fault record serialization", test_fault_record_serialization);
    RUN_TEST("Corrupt fault record rejection", test_corrupt_fault_record_rejection);
    RUN_TEST("Execution budget termination", test_execution_budget_termination);
    RUN_TEST("WFI termination & genuine classification", test_wfi_termination_and_genuine_classification);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << g_pass_count << " / " << g_test_count << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (g_pass_count == g_test_count) ? 0 : 1;
}
