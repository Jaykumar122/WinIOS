#include "cpu/cpu.h"
#include "cpu/debug_architecture.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at line " << __LINE__    \
                      << std::endl;                                            \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

// Drain all events currently available in the TraceManager ring buffer
std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> results;
    TraceEvent ev{};
    auto* rb = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (!rb) {
        return results;
    }
    while (rb->try_pop(ev)) {
        results.push_back(ev);
    }
    return results;
}

constexpr std::uint32_t enc_nop() {
    return 0xD503201Fu;
}

// MRS Xt, sysreg
constexpr std::uint32_t enc_mrs(unsigned rt, unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned op0 = 3) {
    const unsigned o0 = (op0 == 3) ? 1u : 0u;
    return 0xD5300000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

// MSR sysreg, Xt
constexpr std::uint32_t enc_msr(unsigned op1, unsigned crn, unsigned crm, unsigned op2, unsigned rt, unsigned op0 = 3) {
    const unsigned o0 = (op0 == 3) ? 1u : 0u;
    return 0xD5100000u | (o0 << 19u) | ((op1 & 7u) << 16u) | ((crn & 0xFu) << 12u) |
           ((crm & 0xFu) << 8u) | ((op2 & 7u) << 5u) | (rt & 0x1Fu);
}

void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t instruction) {
    memory.write_u32(address, instruction);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 9: System Register Trace Unit Tests ===\n\n";

    // Common setup
    Memory memory(0x100000);
    TraceManager mgr;
    auto cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 4096;
    mgr.configure(cfg);
    mgr.start();

    CPU cpu;
    cpu.bind_trace_manager(&mgr);
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_vbar_el1(0x80000);
    cpu.set_vbar_el2(0x90000);
    cpu.set_vbar_el3(0xA0000);

    // =========================================================================
    // Group A: MRS (Register Read) Tests (Tests 1-10)
    // =========================================================================
    std::cout << "  Group A: MRS (Register Read) Tests (1-10)... " << std::flush;

    // 1. Supported MRS emits exactly one SystemRegisterAccess event
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_daif(0x5u); // D=1, I=0, F=1 -> DAIF register value is (5 << 6) = 0x140
    cpu.set_program_counter(0x1000);
    write_instruction(memory, 0x1000, enc_mrs(1, 3, 4, 2, 1)); // MRS X1, DAIF (op0=3, op1=3, crn=4, crm=2, op2=1)
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(1) == 0x140ULL);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        // Expect: SystemRegisterAccess (seq N), InstructionRetired (seq N+1)
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::InstructionRetired);

        // 2 & 3. Read direction: IsWrite is NOT set
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsWrite));

        // 4. Correct system-register encoding (key for DAIF is 0x5A11)
        TEST_ASSERT(evs[0].address == 0x5A11ULL);

        // 5. Correct register value recorded
        TEST_ASSERT(evs[0].data == 0x140ULL);

        // 6. Original instruction PC recorded
        TEST_ASSERT(evs[0].pc == 0x1000ULL);

        // 7. Correct core ID
        TEST_ASSERT(evs[0].core_id == cpu.get_core_id());

        // 8. Correct Exception Level (EL1) & IsPrivileged flag
        TEST_ASSERT(evs[0].exception_level == 1);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsPrivileged));
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::HasData));
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::HasVirtualAddress));

        // 9. Destination register receives original value
        TEST_ASSERT(cpu.read_register(1) == 0x140ULL);

        // 10. Access size is 8 bytes
        TEST_ASSERT(evs[0].access_size == 8);
    }
    std::cout << "PASS\n";

    // =========================================================================
    // Group B: MSR (Register Write) Tests (Tests 11-18)
    // =========================================================================
    std::cout << "  Group B: MSR (Register Write) Tests (11-18)... " << std::flush;

    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.write_register(2, 0xAu << 28u); // Set N and C flags in NZCV (bits [31:28] = 0xA)
    cpu.set_program_counter(0x2000);
    write_instruction(memory, 0x2000, enc_msr(3, 4, 2, 0, 2)); // MSR NZCV, X2 (key = 0x5A10)
    cpu.step(memory);

    TEST_ASSERT(cpu.get_nzcv() == 0xAu);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);

        // 11. Emits exactly one SystemRegisterAccess event
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::InstructionRetired);

        // 12 & 13. Write direction: IsWrite IS set
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));

        // 14. Original source register value recorded
        TEST_ASSERT(evs[0].data == (0xAu << 28u));

        // 15. Correct system-register encoding (NZCV key = 0x5A10)
        TEST_ASSERT(evs[0].address == 0x5A10ULL);

        // 16. Backing state updated correctly
        TEST_ASSERT(cpu.get_nzcv() == 0xAu);

        // 17. Original instruction PC recorded
        TEST_ASSERT(evs[0].pc == 0x2000ULL);

        // 18. Flags contain IsPrivileged and HasData
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsPrivileged));
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::HasData));
    }
    std::cout << "PASS\n";

    // =========================================================================
    // Group C: Access Validation, Privilege & Traps (Tests 19-30)
    // =========================================================================
    std::cout << "  Group C: Privilege, Access Validation & Traps (19-30)... " << std::flush;

    // 19. EL0 permitted access (NZCV read from EL0)
    pop_all_events(mgr);
    cpu.set_current_el(0);
    cpu.set_program_counter(0x3000);
    write_instruction(memory, 0x3000, enc_mrs(3, 3, 4, 2, 0)); // MRS X3, NZCV at EL0
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(3) == (0xAu << 28u));
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].exception_level == 0);
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsPrivileged));
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsWrite));
        TEST_ASSERT(evs[0].data == (0xAu << 28u));
    }

    // 20. EL0 trapped / undefined access (CurrentEL read from EL0 -> Undefined Instruction)
    pop_all_events(mgr);
    cpu.set_current_el(0);
    cpu.set_program_counter(0x3100);
    write_instruction(memory, 0x3100, enc_mrs(4, 0, 4, 2, 2)); // MRS X4, CurrentEL at EL0 (prohibited)
    cpu.step(memory);

    TEST_ASSERT(cpu.get_current_el() == 1); // Raised to EL1
    TEST_ASSERT(cpu.get_program_counter() == 0x80400); // Lower EL sync vector
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        // Only ExceptionEntered emitted; NO SystemRegisterAccess, NO InstructionRetired
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[0].pc == 0x3100ULL);
    }

    // 21. EL1 access (SCTLR_EL1 read & write at EL1)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.write_register(5, 0x1004ULL); // Value for SCTLR_EL1 (M=0)
    cpu.set_program_counter(0x4000);
    write_instruction(memory, 0x4000, enc_msr(0, 1, 0, 0, 5)); // MSR SCTLR_EL1, X5 (key = 0x4080)
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));
        TEST_ASSERT(evs[0].address == 0x4080ULL);
        TEST_ASSERT(evs[0].data == 0x1004ULL);
    }

    // 22. EL2 access (HCR_EL2 read/write at EL2)
    pop_all_events(mgr);
    cpu.set_current_el(2);
    cpu.write_register(6, 1ULL << 31u); // RW=1 in HCR_EL2
    cpu.set_program_counter(0x4100);
    write_instruction(memory, 0x4100, enc_msr(4, 1, 1, 0, 6)); // MSR HCR_EL2, X6 (key = 0x6088)
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].address == 0x6088ULL);
        TEST_ASSERT(evs[0].exception_level == 2);
        TEST_ASSERT(evs[0].data == (1ULL << 31u));
    }

    // 23. EL3 access (SCR_EL3 read/write at EL3)
    pop_all_events(mgr);
    cpu.set_current_el(3);
    cpu.write_register(7, 1ULL << 0u); // NS bit in SCR_EL3
    cpu.set_program_counter(0x4200);
    write_instruction(memory, 0x4200, enc_msr(6, 1, 1, 0, 7)); // MSR SCR_EL3, X7 (key = 0x7088)
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].address == 0x7088ULL);
        TEST_ASSERT(evs[0].exception_level == 3);
    }

    // 24. Read-only register write rejection (MSR CurrentEL -> Undefined Instruction, zero access event)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.write_register(8, 0x4ULL);
    cpu.set_program_counter(0x4300);
    write_instruction(memory, 0x4300, enc_msr(0, 4, 2, 2, 8)); // MSR CurrentEL, X8 (read-only)
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200); // Same EL sync vector
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
    }

    // 25. Write-only register read rejection (MRS ICC_EOIR1_EL1 -> Undefined Instruction)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x4400);
    write_instruction(memory, 0x4400, enc_mrs(9, 0, 12, 12, 1)); // MRS X9, ICC_EOIR1_EL1 (write-only)
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
    }

    // 26. Unsupported / reserved system-register access
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x4500);
    write_instruction(memory, 0x4500, enc_mrs(10, 0, 15, 15, 7)); // Reserved sysreg encoding
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
    }

    // 27. Trapped MRS produces NO successful access event (PMU trapped to EL2 by MDCR_EL2.TPM)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_mdcr_el2(1ULL << 6u); // MDCR_EL2.TPM = 1 (trap PMU accesses to EL2)
    cpu.set_program_counter(0x4600);
    write_instruction(memory, 0x4600, enc_mrs(11, 3, 9, 12, 0)); // MRS X11, PMCR_EL0
    cpu.step(memory);

    TEST_ASSERT(cpu.get_current_el() == 2); // Trapped to EL2
    TEST_ASSERT(cpu.get_program_counter() == 0x90400); // Lower EL sync vector at EL2
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT((evs[0].opcode_or_syndrome >> 26u) == 0x18u); // EC = 0x18 (Trapped MRS/MSR)
    }
    cpu.set_mdcr_el2(0); // Clear trap

    // 28. Trapped MSR produces NO successful access event (PMU write trapped to EL3 by MDCR_EL3.TPM)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_mdcr_el3(1ULL << 6u); // MDCR_EL3.TPM = 1 (trap PMU to EL3)
    cpu.set_program_counter(0x4700);
    write_instruction(memory, 0x4700, enc_msr(3, 9, 12, 0, 11)); // MSR PMCR_EL0, X11
    cpu.step(memory);

    TEST_ASSERT(cpu.get_current_el() == 3); // Trapped to EL3
    TEST_ASSERT(cpu.get_program_counter() == 0xA0400); // Lower EL sync vector at EL3
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT((evs[0].opcode_or_syndrome >> 26u) == 0x18u);
    }
    cpu.set_mdcr_el3(0); // Clear trap

    // 29. Debug register MRS/MSR (Debug space op0=2: MDSCR_EL1)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.write_register(12, 0x1ULL); // MDSCR_EL1.SS = 1
    cpu.set_program_counter(0x4800);
    write_instruction(memory, 0x4800, enc_msr(0, 0, 2, 2, 12, 2)); // MSR MDSCR_EL1, X12 (op0=2)
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].address == 0x0012ULL); // Key for MDSCR_EL1
        TEST_ASSERT(evs[0].data == 0x1ULL);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));
    }

    // 30. Debug register read (MRS MDSCR_EL1)
    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x4804);
    write_instruction(memory, 0x4804, enc_mrs(13, 0, 0, 2, 2, 2)); // MRS X13, MDSCR_EL1 (op0=2)
    cpu.step(memory);

    TEST_ASSERT(cpu.read_register(13) == 0x1ULL);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].address == 0x0012ULL);
        TEST_ASSERT(evs[0].data == 0x1ULL);
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsWrite));
    }
    std::cout << "PASS\n";

    // =========================================================================
    // Group D: Filtering & Independence Tests (Tests 31-35)
    // =========================================================================
    std::cout << "  Group D: Filtering & Event Independence (31-35)... " << std::flush;

    // 31. System-register tracing disabled via TraceManager stop
    pop_all_events(mgr);
    const auto stats_before = mgr.statistics();
    mgr.stop();
    cpu.set_current_el(1);
    cpu.set_program_counter(0x5000);
    write_instruction(memory, 0x5000, enc_mrs(14, 3, 4, 2, 0)); // MRS X14, NZCV
    cpu.step(memory);

    TEST_ASSERT(mgr.statistics().events_accepted == stats_before.events_accepted);
    TEST_ASSERT(pop_all_events(mgr).empty());

    // 32. SystemRegisterAccess filtered out: InstructionRetired still emitted!
    pop_all_events(mgr);
    mgr.stop();
    auto filter_cfg = TraceConfig::default_enabled_config();
    filter_cfg.ring_capacity = 4096;
    filter_cfg.event_types.set(TraceEventType::SystemRegisterAccess, false);
    TEST_ASSERT(mgr.configure(filter_cfg));
    TEST_ASSERT(mgr.start());

    cpu.set_current_el(1);
    cpu.set_program_counter(0x5100);
    write_instruction(memory, 0x5100, enc_mrs(15, 3, 4, 2, 0)); // MRS X15, NZCV
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
    }

    // 33. InstructionRetired filtered out: SystemRegisterAccess still emitted!
    pop_all_events(mgr);
    mgr.stop();
    filter_cfg.event_types.set(TraceEventType::SystemRegisterAccess, true);
    filter_cfg.event_types.set(TraceEventType::InstructionRetired, false);
    TEST_ASSERT(mgr.configure(filter_cfg));
    TEST_ASSERT(mgr.start());

    cpu.set_current_el(1);
    cpu.set_program_counter(0x5200);
    write_instruction(memory, 0x5200, enc_mrs(16, 3, 4, 2, 0)); // MRS X16, NZCV
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::SystemRegisterAccess);
        TEST_ASSERT(evs[0].pc == 0x5200ULL);
    }

    // 34. Core filtering: access on disabled core produces no event
    pop_all_events(mgr);
    mgr.stop();
    filter_cfg.event_types.set_all(true);
    filter_cfg.core_mask = 0x2u; // Core 1 enabled, Core 0 disabled
    TEST_ASSERT(mgr.configure(filter_cfg));
    TEST_ASSERT(mgr.start());

    cpu.set_current_el(1);
    cpu.set_program_counter(0x5300);
    write_instruction(memory, 0x5300, enc_mrs(17, 3, 4, 2, 0)); // MRS on core 0
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.empty());
    }

    // Restore full configuration
    mgr.stop();
    cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 4096;
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    // 35. Exception tracing independence: trapped access emits ExceptionEntered even when SystemRegisterAccess enabled
    pop_all_events(mgr);
    cpu.set_current_el(0);
    cpu.set_program_counter(0x5400);
    write_instruction(memory, 0x5400, enc_mrs(18, 0, 4, 2, 2)); // MRS CurrentEL at EL0
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
    }
    std::cout << "PASS\n";

    // =========================================================================
    // Group E: Pipeline and Multiple Accesses (Tests 36-38)
    // =========================================================================
    std::cout << "  Group E: Pipeline & Multiple Accesses (36-38)... " << std::flush;

    pop_all_events(mgr);
    cpu.set_current_el(1);
    cpu.set_program_counter(0x6000);
    // Write 4 instructions: MSR NZCV, MRS X19 NZCV, MSR DAIF, MRS X20 DAIF
    write_instruction(memory, 0x6000, enc_msr(3, 4, 2, 0, 0));
    write_instruction(memory, 0x6004, enc_mrs(19, 3, 4, 2, 0));
    write_instruction(memory, 0x6008, enc_msr(3, 4, 2, 1, 0));
    write_instruction(memory, 0x600C, enc_mrs(20, 3, 4, 2, 1));

    cpu.step(memory);
    cpu.step(memory);
    cpu.step(memory);
    cpu.step(memory);

    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        // 4 instructions * 2 events each = 8 events
        TEST_ASSERT(evs.size() == 8);

        // Verify strictly monotonic sequence numbering
        for (std::size_t i = 1; i < evs.size(); ++i) {
            TEST_ASSERT(evs[i].sequence_number > evs[i - 1].sequence_number);
        }

        // Verify event types: [SysReg, Retired, SysReg, Retired, ...]
        for (std::size_t i = 0; i < 4; ++i) {
            TEST_ASSERT(evs[i * 2].get_event_type() == TraceEventType::SystemRegisterAccess);
            TEST_ASSERT(evs[i * 2 + 1].get_event_type() == TraceEventType::InstructionRetired);
            // SysReg and Retired share the same instruction PC
            TEST_ASSERT(evs[i * 2].pc == evs[i * 2 + 1].pc);
            TEST_ASSERT(evs[i * 2].pc == 0x6000ULL + i * 4);
        }

        // Verify write vs read directions
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsWrite));  // MSR
        TEST_ASSERT(!evs[2].has_flag(TraceEventFlags::IsWrite)); // MRS
        TEST_ASSERT(evs[4].has_flag(TraceEventFlags::IsWrite));  // MSR
        TEST_ASSERT(!evs[6].has_flag(TraceEventFlags::IsWrite)); // MRS
    }
    std::cout << "PASS\n";

    mgr.stop();
    std::cout << "\n=== All System Register Trace Unit Tests PASSED Successfully! ===\n";
    return 0;
}
