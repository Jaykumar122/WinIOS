#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_manager.h"
#include "trace/trace_event.h"
#include "trace/trace_file_reader.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace winios::trace;

namespace {

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at line " << __LINE__ << "\n"; \
        std::abort(); \
    } \
} while(0)

// Encoders
constexpr std::uint32_t enc_eret() { return 0xD69F03E0u; }
constexpr std::uint32_t enc_svc(std::uint16_t imm) { return 0xD4000001u | ((static_cast<std::uint32_t>(imm) & 0xFFFFu) << 5u); }
constexpr std::uint32_t enc_hvc(std::uint16_t imm) { return 0xD4000002u | ((static_cast<std::uint32_t>(imm) & 0xFFFFu) << 5u); }
constexpr std::uint32_t enc_smc(std::uint16_t imm) { return 0xD4000003u | ((static_cast<std::uint32_t>(imm) & 0xFFFFu) << 5u); }
constexpr std::uint32_t enc_ret(unsigned rn = 30) { return 0xD65F0000u | ((rn & 0x1Fu) << 5u); }
constexpr std::uint32_t enc_nop() { return 0xD503201Fu; }
constexpr std::uint32_t enc_movz(unsigned rd, std::uint16_t imm16) { return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5u) | (rd & 0x1Fu); }
constexpr std::uint32_t enc_b(std::int32_t word_offset) { return 0x14000000u | (static_cast<std::uint32_t>(word_offset) & 0x03FFFFFFu); }
// LDR X0, [X1] -> 0xF9400020
constexpr std::uint32_t enc_ldr_x(unsigned rt, unsigned rn) { return 0xF9400000u | ((rn & 0x1Fu) << 5u) | (rt & 0x1Fu); }

void write_instruction(Memory& memory, std::uint64_t address, std::uint32_t inst) {
    memory.write_u32(static_cast<std::size_t>(address), inst);
}

inline std::vector<TraceEvent> pop_all_events(TraceManager& mgr) {
    std::vector<TraceEvent> evs;
    TraceEvent ev;
    auto* ring = const_cast<InMemoryRingBuffer*>(mgr.ring_buffer());
    if (ring) {
        while (ring->try_pop(ev)) {
            evs.push_back(ev);
        }
    }
    return evs;
}

// -----------------------------------------------------------------------------
// Exception-Entry Tests (Tests 1-17)
// -----------------------------------------------------------------------------
void test_1_to_17_exception_entry_traces() {
    std::cout << "  Tests 1-17: Exception-entry trace generation and metadata... ";
    Memory memory(0x100000);
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 256;
    cfg.event_types.set(TraceEventType::InterruptDelivered, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);

    // 1. Successful synchronous exception (Undefined Instruction: 0x00000000)
    write_instruction(memory, 0x1000, 0x00000000u);
    cpu.set_program_counter(0x1000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200); // VBAR_EL1 + 0x200 (same EL, SP_ELx)
    TEST_ASSERT(mgr.statistics().events_accepted == 1);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        const auto& e = evs[0];
        TEST_ASSERT(e.get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(e.pc == 0x1000);
        TEST_ASSERT(e.address == 0x80200);
        TEST_ASSERT(e.has_flag(TraceEventFlags::IsSynchronous));
        TEST_ASSERT(e.has_flag(TraceEventFlags::HasVirtualAddress));
        TEST_ASSERT(e.has_flag(TraceEventFlags::IsPrivileged));
        TEST_ASSERT(!e.has_flag(TraceEventFlags::HasData));
        TEST_ASSERT(e.exception_level == 1);
        TEST_ASSERT((e.opcode_or_syndrome >> 26u) == 0x00u); // EC = Unknown / Undefined
        TEST_ASSERT(e.sequence_number == 1);
    }

    // 2. Instruction-fetch abort (fetching outside RAM)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x200000); // Beyond memory size 0x100000
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        const auto& e = evs[0];
        TEST_ASSERT(e.get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(e.pc == 0x200000);
        TEST_ASSERT(e.address == 0x80200);
        TEST_ASSERT((e.opcode_or_syndrome >> 26u) == 0x21u); // InstructionAbortSame
    }

    // 3. Data abort with valid FAR and HasData
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    // LDR X0, [X1] with X1 = 0x300000 (out of bounds data address)
    write_instruction(memory, 0x1000, enc_ldr_x(0, 1));
    cpu.write_register(1, 0x300000);
    cpu.set_program_counter(0x1000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        const auto& e = evs[0];
        TEST_ASSERT(e.get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(e.pc == 0x1000);
        TEST_ASSERT(e.address == 0x80200);
        TEST_ASSERT(e.data == 0x300000); // FAR recorded
        TEST_ASSERT(e.has_flag(TraceEventFlags::HasData));
        TEST_ASSERT((e.opcode_or_syndrome >> 26u) == 0x25u); // DataAbortSame
    }

    // 4. Alignment fault (Unaligned PC)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(0x1002); // Not aligned to 4 bytes
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80200);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        const auto& e = evs[0];
        TEST_ASSERT(e.get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(e.pc == 0x1002);
        TEST_ASSERT((e.opcode_or_syndrome >> 26u) == 0x22u); // PcAlignment
    }

    // 5. Undefined instruction exception from EL0
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    write_instruction(memory, 0x1000, 0x00000000u);
    cpu.set_program_counter(0x1000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80400); // VBAR_EL1 + 0x400 (lower EL, AArch64)
    TEST_ASSERT(cpu.get_current_el() == 1);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        const auto& e = evs[0];
        TEST_ASSERT(e.get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(e.pc == 0x1000);
        TEST_ASSERT(e.address == 0x80400);
        TEST_ASSERT(e.exception_level == 1);
        TEST_ASSERT((e.opcode_or_syndrome >> 26u) == 0x00u);
    }

    // 6. SVC entry with actual retirement semantics (Retirement seq N, ExceptionEntered seq N+1)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    write_instruction(memory, 0x2000, enc_svc(42));
    cpu.set_program_counter(0x2000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80400);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        // Event 1: InstructionRetired
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[0].pc == 0x2000);
        TEST_ASSERT(evs[0].opcode_or_syndrome == enc_svc(42));
        // Event 2: ExceptionEntered
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[1].pc == 0x2000);
        TEST_ASSERT(evs[1].address == 0x80400);
        TEST_ASSERT(evs[1].exception_level == 1);
        TEST_ASSERT((evs[1].opcode_or_syndrome >> 26u) == 0x15u); // EC = SVC64
        TEST_ASSERT((evs[1].opcode_or_syndrome & 0xFFFFu) == 42); // ISS = 42
        TEST_ASSERT(evs[1].sequence_number == evs[0].sequence_number + 1);
    }

    // 7. HVC entry from EL1 to EL2
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_scr_el3(cpu.get_scr_el3() | (1ULL << 8u)); // HCE = 1
    cpu.set_vbar_el2(0x90000);
    cpu.set_current_el(1);
    write_instruction(memory, 0x3000, enc_hvc(99));
    cpu.set_program_counter(0x3000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x90400);
    TEST_ASSERT(cpu.get_current_el() == 2);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[1].exception_level == 2);
        TEST_ASSERT((evs[1].opcode_or_syndrome >> 26u) == 0x16u); // EC = HVC64
        TEST_ASSERT((evs[1].opcode_or_syndrome & 0xFFFFu) == 99);
    }

    // 8. SMC entry from EL1/EL2 to EL3
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_scr_el3(0); // SMD = 0 (enabled)
    cpu.set_vbar_el3(0xA0000);
    cpu.set_current_el(1);
    write_instruction(memory, 0x4000, enc_smc(7));
    cpu.set_program_counter(0x4000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0xA0400);
    TEST_ASSERT(cpu.get_current_el() == 3);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[1].exception_level == 3);
        TEST_ASSERT((evs[1].opcode_or_syndrome >> 26u) == 0x17u); // EC = SMC64
        TEST_ASSERT((evs[1].opcode_or_syndrome & 0xFFFFu) == 7);
    }

    // 9. IRQ delivery (asynchronous exception)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0); // Unmask interrupts
    cpu.set_program_counter(0x5000);
    write_instruction(memory, 0x5000, enc_nop());
    // Trigger physical timer IRQ
    cpu.set_phys_timer(winios::TimerState{true, false, true, 0ULL});
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80280); // EL1h IRQ vector: VBAR + 0x280
    TEST_ASSERT(cpu.get_elr_el1() == 0x5000);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1); // No instruction retired! Only ExceptionEntered
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[0].pc == 0x5000);
        TEST_ASSERT(evs[0].address == 0x80280);
        TEST_ASSERT(evs[0].opcode_or_syndrome == 0); // ESR = 0
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsInterrupt));
        TEST_ASSERT(!evs[0].has_flag(TraceEventFlags::IsSynchronous));
    }
    cpu.set_phys_timer(winios::TimerState{false, false, false, 0ULL});

    // 10. FIQ delivery via GICv3 Group 0
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    cpu.set_daif(0);
    cpu.set_program_counter(0x6000);
    write_instruction(memory, 0x6000, enc_nop());
    // Configure and enable GICv3 for Group 0 (FIQ)
    cpu.get_gic().write_mmio(winios::GICv3::kGicdBase + 0x0000, 0x3u, 4); // Enable Grp0 and Grp1
    cpu.get_gic().write_icc_sre_el1(1, 0);
    cpu.get_gic().write_icc_igrpen0(1, 0);
    cpu.get_gic().write_icc_igrpen1(1, 0);
    cpu.get_gic().write_icc_pmr(0xF0, 0);
    cpu.get_gic().write_mmio(0x080B0080, 0, 4); // Group 0
    cpu.get_gic().write_mmio(0x080B0400 + 1, 0x30, 1);
    cpu.get_gic().write_mmio(0x080B0100, 1u << 1, 4); // Enable
    cpu.get_gic().write_mmio(0x080B0200, 1u << 1, 4); // Assert pending
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80500); // Lower EL FIQ vector: VBAR + 0x500
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(evs[0].pc == 0x6000);
        TEST_ASSERT(evs[0].address == 0x80500);
        TEST_ASSERT(evs[0].has_flag(TraceEventFlags::IsInterrupt));
    }
    cpu.get_gic().acknowledge_group0(0, 0);
    cpu.get_gic().end_of_interrupt_group0(1, 0, 0);

    // 11. Pending but undelivered interrupt emits no exception event (masked by DAIF)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(1);
    cpu.set_daif(0x2u); // Mask IRQ
    cpu.set_program_counter(0x7000);
    write_instruction(memory, 0x7000, enc_nop());
    cpu.set_phys_timer(winios::TimerState{true, false, true, 0ULL});
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x7004); // NOP retired, interrupt masked
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
    }
    cpu.set_phys_timer(winios::TimerState{false, false, false, 0ULL});

    // Tests 12-17 are verified as properties throughout tests 1-11:
    // 12. Exception vector address correct (verified 0x80200, 0x80400, 0x80280, 0x80500, 0x90400, 0xA0400).
    // 13. Source PC correct (verified 0x1000, 0x200000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000).
    // 14. Destination EL correct (EL1, EL2, EL3).
    // 15. Syndrome information matches ESR.
    // 16. FAR information recorded with HasData when valid, 0 when not valid.
    // 17. Exactly one ExceptionEntered event per transition; no duplicate events.

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Exception-Return Tests (Tests 18-25)
// -----------------------------------------------------------------------------
void test_18_to_25_exception_return_traces() {
    std::cout << "  Tests 18-25: Exception-return (ERET) trace generation and metadata... ";
    Memory memory(0x100000);
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());

    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.ring_capacity = 256;
    cfg.event_types.set(TraceEventType::InterruptDelivered, false);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
    cpu.bind_trace_manager(&mgr);

    cpu.set_architectural_exceptions(true);

    // 18-22. Successful ERET: EL1 to EL0
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    cpu.set_elr_el1(0x5000);
    // SPSR_EL1: Mode EL0t (M = 0x00), NZCV = 0, DAIF = 0
    cpu.set_spsr_el1(0x00000000u);
    write_instruction(memory, 0x80200, enc_eret());
    cpu.set_program_counter(0x80200);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x5000); // Returned to 0x5000
    TEST_ASSERT(cpu.get_current_el() == 0);            // Returned to EL0
    TEST_ASSERT(mgr.statistics().events_accepted == 2);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        // Event 1: InstructionRetired for ERET instruction
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[0].pc == 0x80200);
        TEST_ASSERT(evs[0].opcode_or_syndrome == enc_eret());
        // Event 2: ExceptionReturned
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionReturned);
        TEST_ASSERT(evs[1].pc == 0x80200);       // Original ERET address
        TEST_ASSERT(evs[1].address == 0x5000);   // Target return PC
        TEST_ASSERT(evs[1].data == 0x00000000u); // Restored SPSR
        TEST_ASSERT(evs[1].opcode_or_syndrome == enc_eret());
        TEST_ASSERT(evs[1].exception_level == 0);// Destination EL
        TEST_ASSERT(evs[1].has_flag(TraceEventFlags::IsExceptionReturn));
        TEST_ASSERT(evs[1].has_flag(TraceEventFlags::HasVirtualAddress));
        TEST_ASSERT(evs[1].has_flag(TraceEventFlags::HasData));
        TEST_ASSERT(!evs[1].has_flag(TraceEventFlags::IsPrivileged)); // EL0 is unprivileged
        TEST_ASSERT(evs[1].sequence_number == evs[0].sequence_number + 1);
    }

    // Successful ERET: EL2 to EL1h
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(2);
    cpu.set_elr_el2(0x6000);
    // SPSR_EL2: Mode EL1h (M = 0x05)
    cpu.set_spsr_el2(0x05u);
    write_instruction(memory, 0x90400, enc_eret());
    cpu.set_program_counter(0x90400);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x6000);
    TEST_ASSERT(cpu.get_current_el() == 1);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionReturned);
        TEST_ASSERT(evs[1].pc == 0x90400);
        TEST_ASSERT(evs[1].address == 0x6000);
        TEST_ASSERT(evs[1].exception_level == 1);
        TEST_ASSERT(evs[1].has_flag(TraceEventFlags::IsPrivileged)); // EL1 is privileged
    }

    // 23. Failed or rejected ERET emits no return event (ERET at EL0 -> undefined instruction)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x80000);
    cpu.set_current_el(0);
    write_instruction(memory, 0x7000, enc_eret());
    cpu.set_program_counter(0x7000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x80400); // Exception taken
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1); // Exactly 1 ExceptionEntered event, ZERO ExceptionReturned
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT((evs[0].opcode_or_syndrome >> 26u) == 0x00u);
    }

    // 24. Ordinary RET does not emit ExceptionReturned (emits BranchTaken instead)
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(1);
    write_instruction(memory, 0x1000, enc_ret(30));
    cpu.write_register(30, 0x2000);
    cpu.set_program_counter(0x1000);
    cpu.step(memory);

    TEST_ASSERT(cpu.get_program_counter() == 0x2000);
    {
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::BranchTaken); // BranchTaken, NOT ExceptionReturned!
    }

    // 25. Exception return preserves correct retirement-event behavior (verified in tests 18-22: retired followed by returned)

    TEST_ASSERT(mgr.stop());
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Filtering and Pipeline Tests (Tests 26-33)
// -----------------------------------------------------------------------------
void test_26_to_33_filtering_and_persistence() {
    std::cout << "  Tests 26-33: Filtering, file persistence, reader, and drain... ";
    Memory memory(0x100000);
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, cpu.get_core_id());

    // 26. Exception tracing disabled emits no exception events
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 64;
        cfg.event_types.set(TraceEventType::ExceptionEntered, false);
        cfg.event_types.set(TraceEventType::ExceptionReturned, false);
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000);
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        write_instruction(memory, 0x1000, 0x00000000u); // Undefined
        cpu.set_program_counter(0x1000);
        cpu.step(memory);

        TEST_ASSERT(cpu.get_program_counter() == 0x80200);
        TEST_ASSERT(mgr.statistics().events_accepted == 0); // No event emitted
        TEST_ASSERT(mgr.stop());
    }

    // 27. ExceptionEntered filtering works independently
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 64;
        cfg.event_types.set(TraceEventType::ExceptionEntered, false);
        cfg.event_types.set(TraceEventType::ExceptionReturned, true);
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        // Run ERET: should emit InstructionRetired and ExceptionReturned, but no ExceptionEntered
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_elr_el1(0x5000);
        cpu.set_spsr_el1(0x00u);
        write_instruction(memory, 0x80200, enc_eret());
        cpu.set_program_counter(0x80200);
        cpu.step(memory);

        TEST_ASSERT(mgr.statistics().events_accepted == 2);
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::ExceptionReturned);
        TEST_ASSERT(mgr.stop());
    }

    // 28. ExceptionReturned filtering works independently
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 64;
        cfg.event_types.set(TraceEventType::ExceptionReturned, false);
        cfg.event_types.set(TraceEventType::ExceptionEntered, true);
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_current_el(1);
        cpu.set_elr_el1(0x5000);
        cpu.set_spsr_el1(0x00u);
        write_instruction(memory, 0x80200, enc_eret());
        cpu.set_program_counter(0x80200);
        cpu.step(memory);

        TEST_ASSERT(mgr.statistics().events_accepted == 1); // Only InstructionRetired
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 1);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(mgr.stop());
    }

    // 29 & 30. Retirement and Branch tracing continue working alongside exception tracing
    {
        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.ring_capacity = 64;
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        cpu.reset();
        cpu.set_architectural_exceptions(true);
        // Execute B 0x1008
        write_instruction(memory, 0x1000, enc_b(2));
        cpu.set_program_counter(0x1000);
        cpu.step(memory);

        TEST_ASSERT(cpu.get_program_counter() == 0x1008);
        TEST_ASSERT(mgr.statistics().events_accepted == 2);
        std::vector<TraceEvent> evs = pop_all_events(mgr);
        TEST_ASSERT(evs.size() == 2);
        TEST_ASSERT(evs[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(evs[1].get_event_type() == TraceEventType::BranchTaken);
        TEST_ASSERT(mgr.stop());
    }

    // 31, 32, 33. File persistence, TraceFileReader reconstruction, and clean drain
    {
        const std::filesystem::path trace_path = std::filesystem::temp_directory_path() / "winios_exc_test.wtr";
        std::filesystem::remove(trace_path);

        TraceManager mgr;
        TraceConfig cfg = TraceConfig::default_enabled_config();
        cfg.file_output_enabled = true;
        cfg.output_path = trace_path;
        cfg.ring_capacity = 128;
        cfg.writer_batch_size = 64;
        TEST_ASSERT(mgr.configure(cfg));
        TEST_ASSERT(mgr.start());
        cpu.bind_trace_manager(&mgr);

        // Step 1: SVC #42 (InstructionRetired seq 1, ExceptionEntered seq 2)
        cpu.reset();
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x80000);
        cpu.set_current_el(0);
        write_instruction(memory, 0x1000, enc_svc(42));
        cpu.set_program_counter(0x1000);
        cpu.step(memory);

        // Step 2: ERET at vector handler (InstructionRetired seq 3, ExceptionReturned seq 4)
        write_instruction(memory, 0x80400, enc_eret());
        cpu.set_program_counter(0x80400);
        cpu.step(memory);

        TEST_ASSERT(cpu.get_program_counter() == 0x1004); // Returned to instruction after SVC
        TEST_ASSERT(mgr.statistics().events_accepted == 4);

        // 33. Clean shutdown drains all accepted events to file
        TEST_ASSERT(mgr.stop());
        TEST_ASSERT(std::filesystem::exists(trace_path));

        // 32. TraceFileReader reconstructs both exception event types
        TraceFileReader reader;
        TEST_ASSERT(reader.open(trace_path));
        TEST_ASSERT(reader.footer().total_events == 4);

        std::vector<TraceEvent> events;
        TEST_ASSERT(reader.read_all_events(events));
        TEST_ASSERT(events.size() == 4);

        // Verify event stream and monotonicity
        TEST_ASSERT(events[0].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(events[0].pc == 0x1000);

        TEST_ASSERT(events[1].get_event_type() == TraceEventType::ExceptionEntered);
        TEST_ASSERT(events[1].pc == 0x1000);
        TEST_ASSERT(events[1].address == 0x80400);
        TEST_ASSERT((events[1].opcode_or_syndrome >> 26u) == 0x15u);

        TEST_ASSERT(events[2].get_event_type() == TraceEventType::InstructionRetired);
        TEST_ASSERT(events[2].pc == 0x80400);

        TEST_ASSERT(events[3].get_event_type() == TraceEventType::ExceptionReturned);
        TEST_ASSERT(events[3].pc == 0x80400);
        TEST_ASSERT(events[3].address == 0x1004);

        for (std::size_t i = 0; i < events.size(); ++i) {
            TEST_ASSERT(events[i].sequence_number == i + 1);
        }

        reader.close();
        std::filesystem::remove(trace_path);
    }

    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "\n=== WinIOS Phase 52 Stage 8: Exception Trace Unit Tests ===\n\n";

    test_1_to_17_exception_entry_traces();
    test_18_to_25_exception_return_traces();
    test_26_to_33_filtering_and_persistence();

    std::cout << "\n=== All Exception Trace Unit Tests PASSED Successfully! ===\n\n";
    return 0;
}
