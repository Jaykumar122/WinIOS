#include "cpu/cpu.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "trace/trace_config.h"
#include "trace/trace_manager.h"
#include "trace/trace_types.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":"   \
                      << __LINE__ << std::endl;                                \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

using namespace winios;
using namespace winios::trace;

namespace {

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

constexpr std::uint32_t enc_svc(std::uint16_t imm16) {
    return 0xD4000001u | (static_cast<std::uint32_t>(imm16) << 5u);
}

constexpr std::uint32_t enc_hvc(std::uint16_t imm16) {
    return 0xD4000002u | (static_cast<std::uint32_t>(imm16) << 5u);
}

constexpr std::uint32_t enc_smc(std::uint16_t imm16) {
    return 0xD4000003u | (static_cast<std::uint32_t>(imm16) << 5u);
}

constexpr std::uint32_t enc_eret() {
    return 0xD69F03E0u;
}

// MSR SPSel, #imm1: op1=0, op2=5, Rt=0x1F, CRm=(imm1 & 1)
constexpr std::uint32_t enc_msr_spsel_imm(unsigned imm1) {
    return 0xD50040BFu | ((imm1 & 1u) << 8u);
}

// MSR SPSel, Xt: op0=3, op1=0, CRn=4, CRm=2, op2=0, Rt=rt
constexpr std::uint32_t enc_msr_spsel_reg(unsigned rt) {
    return 0xD5184200u | (rt & 0x1Fu);
}

constexpr std::uint32_t enc_add_imm(unsigned rd, unsigned rn, std::uint16_t imm12) {
    return 0x91000000u | ((imm12 & 0xFFFu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

void setup_test_cpu(CPU& cpu, Memory& mem, unsigned el = 1, std::uint64_t vbar = 0x10000) {
    (void)mem;
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(el);
    cpu.set_vbar_el1(vbar);
    cpu.set_vbar_el2(vbar + 0x10000);
    cpu.set_vbar_el3(vbar + 0x20000);
    cpu.set_daif(0);
}

void setup_manager_with_context_change(TraceManager& mgr) {
    TraceConfig cfg = TraceConfig::default_enabled_config();
    cfg.event_types.set(TraceEventType::ContextChanged, true);
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());
}

// Helper unpacking data field:
// prev_el: [1:0], new_el: [3:2], prev_spsel: [4], new_spsel: [5], prev_daif: [11:8], new_daif: [15:12]
struct ContextChangeData {
    unsigned prev_el;
    unsigned new_el;
    bool prev_spsel;
    bool new_spsel;
    std::uint8_t prev_daif;
    std::uint8_t new_daif;
};

ContextChangeData unpack_data(std::uint64_t data) {
    ContextChangeData d{};
    d.prev_el = static_cast<unsigned>(data & 0x3u);
    d.new_el = static_cast<unsigned>((data >> 2u) & 0x3u);
    d.prev_spsel = ((data >> 4u) & 0x1u) != 0;
    d.new_spsel = ((data >> 5u) & 0x1u) != 0;
    d.prev_daif = static_cast<std::uint8_t>((data >> 8u) & 0xFu);
    d.new_daif = static_cast<std::uint8_t>((data >> 12u) & 0xFu);
    return d;
}

// -----------------------------------------------------------------------------
// Test 17: SVC from EL0 to EL1
// -----------------------------------------------------------------------------
void test_svc_el0_to_el1() {
    std::cout << "  Test 17: SVC from EL0 to EL1 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x1000, enc_svc(0));

    CPU cpu;
    setup_test_cpu(cpu, mem, 0, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x1000);
            TEST_ASSERT(ev.address == 0x10400); // lower EL, AArch64 vector offset
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 0);
            TEST_ASSERT(d.new_el == 1);
            TEST_ASSERT(d.new_spsel == true);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsInterrupt)) == 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsPrivileged)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 18: SVC from EL1 to EL1
// -----------------------------------------------------------------------------
void test_svc_el1_to_el1() {
    std::cout << "  Test 18: SVC from EL1 to EL1 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x2000, enc_svc(1));

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x2000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x2000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 1);
            TEST_ASSERT(d.new_el == 1);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 19: HVC from EL1 to EL2
// -----------------------------------------------------------------------------
void test_hvc_el1_to_el2() {
    std::cout << "  Test 19: HVC from EL1 to EL2 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x3000, enc_hvc(0));

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x3000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x3000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 1);
            TEST_ASSERT(d.new_el == 2);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 20: SMC from EL1 to EL3
// -----------------------------------------------------------------------------
void test_smc_el1_to_el3() {
    std::cout << "  Test 20: SMC from EL1 to EL3 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x4000, enc_smc(0));

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x4000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x4000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 1);
            TEST_ASSERT(d.new_el == 3);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 21: ERET from EL1 to EL0
// -----------------------------------------------------------------------------
void test_eret_el1_to_el0() {
    std::cout << "  Test 21: ERET from EL1 to EL0 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x5000, enc_eret());

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x5000);
    cpu.set_spsr_el1(0x0); // Mode EL0t (M[3:0] = 0)
    cpu.set_elr_el1(0x8000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x5000);
            TEST_ASSERT(ev.address == 0x8000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionReturn));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 1);
            TEST_ASSERT(d.new_el == 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsExceptionReturn)) != 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsPrivileged)) == 0); // EL0 is unprivileged
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 22: ERET from EL2 to EL1
// -----------------------------------------------------------------------------
void test_eret_el2_to_el1() {
    std::cout << "  Test 22: ERET from EL2 to EL1 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x6000, enc_eret());

    CPU cpu;
    setup_test_cpu(cpu, mem, 2, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x6000);
    cpu.set_spsr_el2(0x5); // Mode EL1h (M[3:0] = 5)
    cpu.set_elr_el2(0x9000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x6000);
            TEST_ASSERT(ev.address == 0x9000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionReturn));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 2);
            TEST_ASSERT(d.new_el == 1);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsExceptionReturn)) != 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsPrivileged)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 23: ERET from EL3 to EL1
// -----------------------------------------------------------------------------
void test_eret_el3_to_el1() {
    std::cout << "  Test 23: ERET from EL3 to EL1 emits ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);
    mem.write_u32(0x7000, enc_eret());

    CPU cpu;
    setup_test_cpu(cpu, mem, 3, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x7000);
    cpu.set_spsr_el3(0x5); // Mode EL1h (M[3:0] = 5)
    cpu.set_elr_el3(0xA000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x7000);
            TEST_ASSERT(ev.address == 0xA000);
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionReturn));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_el == 3);
            TEST_ASSERT(d.new_el == 1);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsExceptionReturn)) != 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 24: Asynchronous Timer IRQ Delivery
// -----------------------------------------------------------------------------
void test_timer_irq_context_change() {
    std::cout << "  Test 24: Asynchronous IRQ delivery emits ContextChanged with IsInterrupt... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x40000);

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    // Fire timer interrupt
    cpu.set_phys_timer(TimerState{true, false, true, 0});
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_cc = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found_cc = true;
            TEST_ASSERT(ev.pc == 0x1000);
            TEST_ASSERT(ev.address == 0x10280); // EL1h IRQ vector
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::ExceptionEntry));
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsInterrupt)) != 0);
            TEST_ASSERT((ev.flags & static_cast<std::uint32_t>(TraceEventFlags::IsSynchronous)) == 0);
        }
    }
    TEST_ASSERT(found_cc);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 25: MSR SPSel, #imm1
// -----------------------------------------------------------------------------
void test_msr_spsel_imm() {
    std::cout << "  Test 25: MSR SPSel immediate emits RegisterBankSelect... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_msr_spsel_imm(0)); // MSR SPSel, #0
    mem.write_u32(0x1004, enc_msr_spsel_imm(1)); // MSR SPSel, #1

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);
    cpu.set_sp_sel(true); // Start in SP_ELx

    // 1. Switch to SP_EL0
    cpu.step(mem);
    auto evs1 = pop_all_events(mgr);
    bool found1 = false;
    for (const auto& ev : evs1) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found1 = true;
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::RegisterBankSelect));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_spsel == true);
            TEST_ASSERT(d.new_spsel == false);
        }
    }
    TEST_ASSERT(found1);

    // 2. Switch back to SP_ELx
    cpu.step(mem);
    auto evs2 = pop_all_events(mgr);
    bool found2 = false;
    for (const auto& ev : evs2) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found2 = true;
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::RegisterBankSelect));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_spsel == false);
            TEST_ASSERT(d.new_spsel == true);
        }
    }
    TEST_ASSERT(found2);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 26: MSR SPSel, Xt
// -----------------------------------------------------------------------------
void test_msr_spsel_reg() {
    std::cout << "  Test 26: MSR SPSel register emits RegisterBankSelect... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x10000);
    mem.write_u32(0x2000, enc_msr_spsel_reg(0)); // MSR SPSel, X0
    mem.write_u32(0x2004, enc_msr_spsel_reg(0)); // MSR SPSel, X0

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x2000);
    cpu.set_sp_sel(true);

    // 1. Write X0 = 0
    cpu.write_register(0, 0);
    cpu.step(mem);

    auto evs1 = pop_all_events(mgr);
    bool found1 = false;
    for (const auto& ev : evs1) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found1 = true;
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::RegisterBankSelect));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_spsel == true);
            TEST_ASSERT(d.new_spsel == false);
        }
    }
    TEST_ASSERT(found1);

    // 2. Write X0 = 1
    cpu.write_register(0, 1);
    cpu.step(mem);

    auto evs2 = pop_all_events(mgr);
    bool found2 = false;
    for (const auto& ev : evs2) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ContextChanged)) {
            found2 = true;
            TEST_ASSERT(ev.opcode_or_syndrome == static_cast<std::uint32_t>(ContextChangeReason::RegisterBankSelect));
            auto d = unpack_data(ev.data);
            TEST_ASSERT(d.prev_spsel == false);
            TEST_ASSERT(d.new_spsel == true);
        }
    }
    TEST_ASSERT(found2);
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 27: Idempotent MSR SPSel writes emit NO ContextChanged event
// -----------------------------------------------------------------------------
void test_msr_spsel_idempotent_no_event() {
    std::cout << "  Test 27: Idempotent MSR SPSel does not emit ContextChanged... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x10000);
    mem.write_u32(0x3000, enc_msr_spsel_imm(1)); // Write 1 when already 1

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x3000);
    cpu.set_sp_sel(true);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::ContextChanged));
    }
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 28: Ordinary ALU, Branch, Memory operations emit ZERO ContextChanged events
// -----------------------------------------------------------------------------
void test_ordinary_instructions_no_context_change() {
    std::cout << "  Test 28: Ordinary ALU instructions emit zero ContextChanged events... " << std::flush;
    TraceManager mgr;
    setup_manager_with_context_change(mgr);
    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_add_imm(1, 0, 42));
    mem.write_u32(0x1004, enc_nop());

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    cpu.step(mem);
    cpu.step(mem);

    auto events = pop_all_events(mgr);
    for (const auto& ev : events) {
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::ContextChanged));
    }
    std::cout << "PASS\n";
}

// -----------------------------------------------------------------------------
// Test 29: Event filtering for ContextChanged
// -----------------------------------------------------------------------------
void test_context_change_filtering() {
    std::cout << "  Test 29: ContextChanged event filtering... " << std::flush;
    TraceManager mgr;
    TraceConfig cfg = TraceConfig::default_enabled_config();
    // ContextChanged is disabled by default in default_enabled_config()
    TEST_ASSERT(!cfg.event_types.test(TraceEventType::ContextChanged));
    TEST_ASSERT(mgr.configure(cfg));
    TEST_ASSERT(mgr.start());

    Memory mem(0x10000);
    mem.write_u32(0x1000, enc_svc(0));

    CPU cpu;
    setup_test_cpu(cpu, mem, 1, 0x10000);
    cpu.bind_trace_manager(&mgr);
    cpu.set_program_counter(0x1000);

    cpu.step(mem);

    auto events = pop_all_events(mgr);
    bool found_svc_exception = false;
    for (const auto& ev : events) {
        if (ev.event_type == static_cast<std::uint16_t>(TraceEventType::ExceptionEntered)) {
            found_svc_exception = true;
        }
        TEST_ASSERT(ev.event_type != static_cast<std::uint16_t>(TraceEventType::ContextChanged));
    }
    TEST_ASSERT(found_svc_exception);
    std::cout << "PASS\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Phase 52 Stage 13: Context Change Trace Unit Tests ===\n\n";

    test_svc_el0_to_el1();
    test_svc_el1_to_el1();
    test_hvc_el1_to_el2();
    test_smc_el1_to_el3();
    test_eret_el1_to_el0();
    test_eret_el2_to_el1();
    test_eret_el3_to_el1();
    test_timer_irq_context_change();
    test_msr_spsel_imm();
    test_msr_spsel_reg();
    test_msr_spsel_idempotent_no_event();
    test_ordinary_instructions_no_context_change();
    test_context_change_filtering();

    std::cout << "\nAll 13 context change trace unit tests PASSED.\n";
    return 0;
}
