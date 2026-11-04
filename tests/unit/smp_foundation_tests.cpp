#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "replay/replay_engine.h"
#include "replay/replay_session.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/wsnap_format.h"
#include "sndl/sndl_format.h"
#include "sndl/sndl_writer.h"
#include "trace/trace_event.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace {

#define CHECK(expr)                                                           \
    do {                                                                      \
        if (!(expr)) {                                                        \
            std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ":"     \
                      << __LINE__ << "\n";                                    \
            std::abort();                                                     \
        }                                                                     \
    } while (false)

void log_test(const char* name) {
    std::cout << "[RUNNING] " << name << "\n";
}

void log_pass(const char* name) {
    std::cout << "[PASSED]  " << name << "\n";
}

// -----------------------------------------------------------------------------
// Group A: Core State Isolation Tests
// -----------------------------------------------------------------------------

void test_a1_independent_register_files() {
    log_test("A1: Independent Register Files");
    winios::SmpCoreState core0;
    winios::SmpCoreState core1;
    core0.reset(0, winios::CoreLifecycleState::Runnable);
    core1.reset(1, winios::CoreLifecycleState::Runnable);

    for (std::size_t i = 0; i < 31; ++i) {
        core0.x[i] = 0x1000ULL + i;
        core1.x[i] = 0x2000ULL + i;
    }

    for (std::size_t i = 0; i < 31; ++i) {
        CHECK(core0.x[i] == 0x1000ULL + i);
        CHECK(core1.x[i] == 0x2000ULL + i);
        CHECK(core0.x[i] != core1.x[i]);
    }

    winios::CPU cpu0;
    winios::CPU cpu1;
    core0.apply_to_cpu(cpu0);
    core1.apply_to_cpu(cpu1);

    for (std::size_t i = 0; i < 31; ++i) {
        CHECK(cpu0.read_register(i) == 0x1000ULL + i);
        CHECK(cpu1.read_register(i) == 0x2000ULL + i);
    }
    log_pass("A1: Independent Register Files");
}

void test_a2_independent_pc_and_sp() {
    log_test("A2: Independent Program Counters and Stack Pointers");
    winios::SmpCoreState c0;
    winios::SmpCoreState c1;
    c0.reset(0);
    c1.reset(1);

    c0.pc = 0x0000'8000ULL;
    c1.pc = 0x0000'C000ULL;
    c0.sp_el0 = 0x1000'0000ULL;
    c1.sp_el0 = 0x2000'0000ULL;
    c0.sp_el1 = 0x1000'1000ULL;
    c1.sp_el1 = 0x2000'1000ULL;

    CHECK(c0.pc != c1.pc);
    CHECK(c0.sp_el0 != c1.sp_el0);
    CHECK(c0.sp_el1 != c1.sp_el1);

    winios::CPU cpu0;
    winios::CPU cpu1;
    c0.apply_to_cpu(cpu0);
    c1.apply_to_cpu(cpu1);

    CHECK(cpu0.get_program_counter() == 0x0000'8000ULL);
    CHECK(cpu1.get_program_counter() == 0x0000'C000ULL);
    CHECK(cpu0.get_sp_el0() == 0x1000'0000ULL);
    CHECK(cpu1.get_sp_el0() == 0x2000'0000ULL);
    log_pass("A2: Independent Program Counters and Stack Pointers");
}

void test_a3_independent_pstate_nzcv_daif() {
    log_test("A3: Independent PSTATE, NZCV, and DAIF flags");
    winios::SmpCoreState c0;
    winios::SmpCoreState c1;
    c0.reset(0);
    c1.reset(1);

    c0.nzcv = 0x8u; // N set
    c1.nzcv = 0x4u; // Z set
    c0.daif = 0x0u; // unmasked
    c1.daif = 0xFu; // all masked

    CHECK(c0.nzcv != c1.nzcv);
    CHECK(c0.daif != c1.daif);

    winios::CPU cpu0;
    winios::CPU cpu1;
    c0.apply_to_cpu(cpu0);
    c1.apply_to_cpu(cpu1);

    CHECK(cpu0.get_nzcv() == 0x8u);
    CHECK(cpu1.get_nzcv() == 0x4u);
    CHECK(cpu0.get_daif() == 0x0u);
    CHECK(cpu1.get_daif() == 0xFu);
    log_pass("A3: Independent PSTATE, NZCV, and DAIF flags");
}

void test_a4_independent_simd_fp() {
    log_test("A4: Independent SIMD/FP Registers");
    winios::SmpCoreState c0;
    winios::SmpCoreState c1;
    c0.reset(0);
    c1.reset(1);

    for (std::size_t i = 0; i < 32; ++i) {
        c0.v[i].low = 0x1111ULL * (i + 1);
        c0.v[i].high = 0x2222ULL * (i + 1);
        c1.v[i].low = 0x3333ULL * (i + 1);
        c1.v[i].high = 0x4444ULL * (i + 1);
    }
    c0.fpcr = 0x01000000u;
    c1.fpcr = 0x02000000u;

    winios::CPU cpu0;
    winios::CPU cpu1;
    c0.apply_to_cpu(cpu0);
    c1.apply_to_cpu(cpu1);

    for (std::size_t i = 0; i < 32; ++i) {
        const auto v0 = cpu0.read_vector_register(i);
        const auto v1 = cpu1.read_vector_register(i);
        CHECK(v0.low == c0.v[i].low);
        CHECK(v0.high == c0.v[i].high);
        CHECK(v1.low == c1.v[i].low);
        CHECK(v1.high == c1.v[i].high);
    }
    CHECK(cpu0.get_fpcr() == 0x01000000u);
    CHECK(cpu1.get_fpcr() == 0x02000000u);
    log_pass("A4: Independent SIMD/FP Registers");
}

void test_a5_independent_lifecycle_and_retirement() {
    log_test("A5: Independent Lifecycle and Retirement Counters");
    winios::SmpCoreState c0;
    winios::SmpCoreState c1;
    c0.reset(0, winios::CoreLifecycleState::Runnable);
    c1.reset(1, winios::CoreLifecycleState::Halted);

    c0.instruction_retirement_count = 1000;
    c1.instruction_retirement_count = 500;

    CHECK(c0.lifecycle == winios::CoreLifecycleState::Runnable);
    CHECK(c1.lifecycle == winios::CoreLifecycleState::Halted);
    CHECK(c0.instruction_retirement_count != c1.instruction_retirement_count);

    winios::CPU cpu0;
    winios::CPU cpu1;
    c0.apply_to_cpu(cpu0);
    c1.apply_to_cpu(cpu1);

    CHECK(!cpu0.is_halted());
    CHECK(cpu1.is_halted());
    CHECK(cpu0.get_instruction_retirement_count() == 1000);
    CHECK(cpu1.get_instruction_retirement_count() == 500);
    log_pass("A5: Independent Lifecycle and Retirement Counters");
}

void test_a6_capture_and_apply() {
    log_test("A6: State Capture and Apply Verification");
    winios::CPU cpu;
    cpu.reset();
    cpu.set_core_id(3);
    cpu.write_register(0, 0xCAFE'BABE'DEAD'BEEFULL);
    cpu.write_register(30, 0x1234'5678'9ABC'DEF0ULL);
    cpu.set_program_counter(0x0040'0000ULL);
    cpu.set_sp_el0(0x0080'0000ULL);
    cpu.set_nzcv(0x5u);
    cpu.set_instruction_retirement_count(4242);

    winios::SmpCoreState state;
    state.capture_from_cpu(cpu);

    CHECK(state.core_id == 3);
    CHECK(state.x[0] == 0xCAFE'BABE'DEAD'BEEFULL);
    CHECK(state.x[30] == 0x1234'5678'9ABC'DEF0ULL);
    CHECK(state.pc == 0x0040'0000ULL);
    CHECK(state.sp_el0 == 0x0080'0000ULL);
    CHECK(state.nzcv == 0x5u);
    CHECK(state.instruction_retirement_count == 4242);

    winios::CPU cpu2;
    state.apply_to_cpu(cpu2);

    CHECK(cpu2.get_core_id() == 3);
    CHECK(cpu2.read_register(0) == 0xCAFE'BABE'DEAD'BEEFULL);
    CHECK(cpu2.read_register(30) == 0x1234'5678'9ABC'DEF0ULL);
    CHECK(cpu2.get_program_counter() == 0x0040'0000ULL);
    CHECK(cpu2.get_sp_el0() == 0x0080'0000ULL);
    CHECK(cpu2.get_nzcv() == 0x5u);
    CHECK(cpu2.get_instruction_retirement_count() == 4242);
    log_pass("A6: State Capture and Apply Verification");
}

void test_a7_canonical_state_hash() {
    log_test("A7: Canonical State Hashing & Determinism");
    winios::SmpCoreState s1;
    winios::SmpCoreState s2;
    s1.reset(0, winios::CoreLifecycleState::Runnable);
    s2.reset(0, winios::CoreLifecycleState::Runnable);

    s1.x[5] = 0x12345;
    s2.x[5] = 0x12345;
    s1.pc = 0x8000;
    s2.pc = 0x8000;

    const std::uint64_t h1 = s1.compute_canonical_hash();
    const std::uint64_t h2 = s2.compute_canonical_hash();
    CHECK(h1 == h2);

    // Modify architectural field -> hash MUST change
    s2.x[5] = 0x12346;
    CHECK(s1.compute_canonical_hash() != s2.compute_canonical_hash());

    // Revert and modify transient field -> hash MUST NOT change
    s2.x[5] = 0x12345;
    s2.current_budget = 9999;
    s2.executed_in_quantum = 1234;
    CHECK(s1.compute_canonical_hash() == s2.compute_canonical_hash());
    log_pass("A7: Canonical State Hashing & Determinism");
}

// -----------------------------------------------------------------------------
// Group B: Shared Physical Memory Tests
// -----------------------------------------------------------------------------

void test_b1_cross_core_visibility() {
    log_test("B1: Cross-Core Memory Visibility");
    winios::SmpSharedMemory mem(65536, 4);

    mem.write_u64(0, 0x1000, 0xFEED'BEEF'CAFE'D00DULL);
    const std::uint64_t val = mem.read_u64(1, 0x1000);
    CHECK(val == 0xFEED'BEEF'CAFE'D00DULL);
    log_pass("B1: Cross-Core Memory Visibility");
}

void test_b2_address_identity() {
    log_test("B2: Memory Address Identity");
    winios::SmpSharedMemory smem(65536, 4);
    auto& raw = smem.raw_memory();

    raw.write_u32(0x200, 0x12345678u);
    CHECK(smem.read_u32(0, 0x200) == 0x12345678u);
    CHECK(smem.read_u32(1, 0x200) == 0x12345678u);

    smem.write_u32(2, 0x200, 0x87654321u);
    CHECK(raw.read_u32(0x200) == 0x87654321u);
    log_pass("B2: Memory Address Identity");
}

void test_b3_multi_width_accesses() {
    log_test("B3: Multi-Width Memory Operations (8/16/32/64-bit)");
    winios::SmpSharedMemory smem(65536, 4);

    smem.write_u8(0, 0x300, 0xAAu);
    smem.write_u16(1, 0x302, 0xBBAAu);
    smem.write_u32(2, 0x304, 0xDDCCBBAAu);
    smem.write_u64(3, 0x308, 0x0123456789ABCDEFULL);

    CHECK(smem.read_u8(1, 0x300) == 0xAAu);
    CHECK(smem.read_u16(0, 0x302) == 0xBBAAu);
    CHECK(smem.read_u32(3, 0x304) == 0xDDCCBBAAu);
    CHECK(smem.read_u64(2, 0x308) == 0x0123456789ABCDEFULL);

    std::uint8_t buf[8]{};
    smem.read_bytes(0, 0x308, buf, 8);
    CHECK(buf[0] == 0xEFu);
    CHECK(buf[7] == 0x01u);
    log_pass("B3: Multi-Width Memory Operations (8/16/32/64-bit)");
}

void test_b4_per_core_telemetry() {
    log_test("B4: Per-Core Access Telemetry");
    winios::SmpSharedMemory smem(65536, 4);

    smem.write_u64(0, 0x400, 0x111ULL);
    smem.write_u64(0, 0x408, 0x222ULL);
    smem.read_u64(0, 0x400);

    smem.read_u32(1, 0x400);
    smem.read_u32(1, 0x404);

    const auto t0 = smem.get_telemetry(0);
    CHECK(t0.writes == 2);
    CHECK(t0.reads == 1);
    CHECK(t0.bytes_written == 16);
    CHECK(t0.bytes_read == 8);

    const auto t1 = smem.get_telemetry(1);
    CHECK(t1.writes == 0);
    CHECK(t1.reads == 2);
    CHECK(t1.bytes_written == 0);
    CHECK(t1.bytes_read == 8);
    log_pass("B4: Per-Core Access Telemetry");
}

void test_b5_out_of_bounds() {
    log_test("B5: Memory Out-of-Bounds Protection");
    winios::SmpSharedMemory smem(4096, 2);

    bool caught_read = false;
    try {
        smem.read_u32(0, 4096);
    } catch (const std::out_of_range&) {
        caught_read = true;
    }
    CHECK(caught_read);

    bool caught_write = false;
    try {
        smem.write_u64(1, 4092, 0x123ULL);
    } catch (const std::out_of_range&) {
        caught_write = true;
    }
    CHECK(caught_write);
    log_pass("B5: Memory Out-of-Bounds Protection");
}

// -----------------------------------------------------------------------------
// Group C: Deterministic Global Scheduler Tests
// -----------------------------------------------------------------------------

void test_c1_core_registration() {
    log_test("C1: Scheduler Core Registration");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 4);
    winios::SmpScheduler sched(mem, 4);

    auto cpu0 = std::make_shared<winios::CPU>();
    auto cpu1 = std::make_shared<winios::CPU>();
    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
    sched.register_core(1, cpu1, winios::CoreLifecycleState::Halted);

    CHECK(sched.core_count() == 2);
    CHECK(sched.get_slot(0).state.lifecycle == winios::CoreLifecycleState::Runnable);
    CHECK(sched.get_slot(1).state.lifecycle == winios::CoreLifecycleState::Halted);
    log_pass("C1: Scheduler Core Registration");
}

void test_c2_round_robin_selection() {
    log_test("C2: Round-Robin Selection");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 4);
    winios::SmpScheduler sched(mem, 4, 10); // Q = 10 instructions

    // Populate memory with NOPs (0xD503201F) for each core at different PC
    for (std::size_t offset = 0; offset < 4096; offset += 4) {
        mem->write_u32(0, offset, 0xD503201Fu); // NOP
    }

    auto cpu0 = std::make_shared<winios::CPU>();
    auto cpu1 = std::make_shared<winios::CPU>();
    cpu0->set_program_counter(0x000);
    cpu1->set_program_counter(0x100);

    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
    sched.register_core(1, cpu1, winios::CoreLifecycleState::Runnable);

    const auto rec0 = sched.step_quantum();
    CHECK(rec0.core_id == 0);
    CHECK(rec0.instructions_retired == 10);

    const auto rec1 = sched.step_quantum();
    CHECK(rec1.core_id == 1);
    CHECK(rec1.instructions_retired == 10);

    const auto rec2 = sched.step_quantum();
    CHECK(rec2.core_id == 0);
    CHECK(rec2.instructions_retired == 10);
    log_pass("C2: Round-Robin Selection");
}

void test_c3_quantum_enforcement() {
    log_test("C3: Quantum Enforcement (Q Instructions per Slice)");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 2);
    winios::SmpScheduler sched(mem, 2, 50); // Q = 50

    for (std::size_t offset = 0; offset < 4096; offset += 4) {
        mem->write_u32(0, offset, 0xD503201Fu); // NOP
    }

    auto cpu0 = std::make_shared<winios::CPU>();
    cpu0->set_architectural_exceptions(true);
    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);

    const auto rec = sched.step_quantum();
    CHECK(rec.core_id == 0);
    CHECK(rec.instructions_retired == 50);
    CHECK(rec.reason == winios::QuantumTruncationReason::QuantumExhausted);
    CHECK(cpu0->get_instruction_retirement_count() == 50);
    log_pass("C3: Quantum Enforcement (Q Instructions per Slice)");
}

void test_c4_truncation_on_hlt() {
    log_test("C4: Truncation on HLT Instruction");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 2);
    winios::SmpScheduler sched(mem, 2, 50);

    // 2 NOPs followed by HLT
    mem->write_u32(0, 0x00, 0xD503201Fu); // NOP
    mem->write_u32(0, 0x04, 0xD503201Fu); // NOP
    mem->write_u32(0, 0x08, 0xD4400000u); // HLT #0

    auto cpu0 = std::make_shared<winios::CPU>();
    cpu0->set_architectural_exceptions(true);
    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);

    const auto rec = sched.step_quantum();
    CHECK(rec.core_id == 0);
    CHECK(rec.instructions_retired == 3);
    CHECK(rec.reason == winios::QuantumTruncationReason::CoreHalted);
    CHECK(cpu0->is_halted());
    log_pass("C4: Truncation on HLT Instruction");
}

void test_c5_truncation_on_wfi() {
    log_test("C5: Truncation on WFI Instruction");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 2);
    winios::SmpScheduler sched(mem, 2, 50);

    // 1 NOP followed by WFI
    mem->write_u32(0, 0x00, 0xD503201Fu); // NOP
    mem->write_u32(0, 0x04, 0xD503207Fu); // WFI

    auto cpu0 = std::make_shared<winios::CPU>();
    cpu0->set_architectural_exceptions(true);
    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);

    const auto rec = sched.step_quantum();
    CHECK(rec.core_id == 0);
    CHECK(rec.instructions_retired == 2);
    CHECK(rec.reason == winios::QuantumTruncationReason::CoreWaitingForInterrupt);
    CHECK(cpu0->is_waiting_for_interrupt());
    log_pass("C5: Truncation on WFI Instruction");
}

void test_c6_quiescent_skipping() {
    log_test("C6: Skipping Quiescent / Halted Cores");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 4);
    winios::SmpScheduler sched(mem, 4, 10);

    for (std::size_t offset = 0; offset < 4096; offset += 4) {
        mem->write_u32(0, offset, 0xD503201Fu); // NOP
    }

    auto cpu0 = std::make_shared<winios::CPU>();
    auto cpu1 = std::make_shared<winios::CPU>();
    auto cpu2 = std::make_shared<winios::CPU>();

    cpu0->set_program_counter(0x000);
    cpu1->set_program_counter(0x100);
    cpu2->set_program_counter(0x200);

    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
    sched.register_core(1, cpu1, winios::CoreLifecycleState::Halted); // Halted!
    sched.register_core(2, cpu2, winios::CoreLifecycleState::Runnable);

    const auto rec0 = sched.step_quantum();
    CHECK(rec0.core_id == 0);

    // Core 1 is halted, scheduler MUST skip to Core 2
    const auto rec1 = sched.step_quantum();
    CHECK(rec1.core_id == 2);

    const auto rec2 = sched.step_quantum();
    CHECK(rec2.core_id == 0);
    log_pass("C6: Skipping Quiescent / Halted Cores");
}

void test_c7_monotonic_global_sequence() {
    log_test("C7: Monotonic Global Retirement Sequence");
    auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 2);
    winios::SmpScheduler sched(mem, 2, 20);

    for (std::size_t offset = 0; offset < 4096; offset += 4) {
        mem->write_u32(0, offset, 0xD503201Fu);
    }

    auto cpu0 = std::make_shared<winios::CPU>();
    auto cpu1 = std::make_shared<winios::CPU>();
    cpu0->set_program_counter(0x000);
    cpu1->set_program_counter(0x100);

    sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
    sched.register_core(1, cpu1, winios::CoreLifecycleState::Runnable);

    const auto rec0 = sched.step_quantum();
    CHECK(rec0.start_global_sequence == 0);
    CHECK(rec0.end_global_sequence == 20);

    const auto rec1 = sched.step_quantum();
    CHECK(rec1.start_global_sequence == 20);
    CHECK(rec1.end_global_sequence == 40);

    CHECK(sched.global_retirement_sequence() == 40);
    log_pass("C7: Monotonic Global Retirement Sequence");
}

void test_c8_repeatability_10_runs() {
    log_test("C8: Deterministic 10-Run Repeatability");
    std::uint64_t baseline_hash = 0;
    std::uint64_t baseline_instructions = 0;

    for (int run = 0; run < 10; ++run) {
        auto mem = std::make_shared<winios::SmpSharedMemory>(65536, 2);
        winios::SmpScheduler sched(mem, 2, 25);

        // Core 0 executes 100 instructions: ADD X0, X0, #1
        for (std::size_t i = 0; i < 100; ++i) {
            mem->write_u32(0, i * 4, 0x91000400u); // ADD X0, X0, #1
        }
        mem->write_u32(0, 100 * 4, 0xD4400000u); // HLT

        // Core 1 executes 50 instructions: ADD X1, X1, #2
        for (std::size_t i = 0; i < 50; ++i) {
            mem->write_u32(1, 0x1000 + i * 4, 0x91000821u); // ADD X1, X1, #2
        }
        mem->write_u32(1, 0x1000 + 50 * 4, 0xD4400000u); // HLT

        auto cpu0 = std::make_shared<winios::CPU>();
        auto cpu1 = std::make_shared<winios::CPU>();
        cpu0->set_program_counter(0x000);
        cpu1->set_program_counter(0x1000);

        sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
        sched.register_core(1, cpu1, winios::CoreLifecycleState::Runnable);

        const std::uint64_t retired = sched.run_deterministic(10000);
        const std::uint64_t hash = sched.compute_canonical_multicore_hash();

        if (run == 0) {
            baseline_instructions = retired;
            baseline_hash = hash;
            std::cout << "  (Retired=" << retired << " Expected=" << (101 + 51) << ")\n";
            CHECK(retired == (101 + 51)); // 100 adds + 1 hlt on core0; 50 adds + 1 hlt on core1
        } else {
            CHECK(retired == baseline_instructions);
            CHECK(hash == baseline_hash);
        }
    }
    log_pass("C8: Deterministic 10-Run Repeatability");
}

// -----------------------------------------------------------------------------
// Group D: ARM Exclusive Monitor Tests
// -----------------------------------------------------------------------------

void test_d1_reservation_creation() {
    log_test("D1: Exclusive Reservation Creation (LDXR)");
    winios::SmpExclusiveMonitorManager mon(4);

    mon.load_exclusive(0, 0x1028, 8);
    CHECK(mon.is_reservation_valid(0, 0x1028, 8));
    CHECK(mon.is_reservation_valid(0, 0x1000, 8)); // Within same 64-byte granule
    CHECK(!mon.is_reservation_valid(1, 0x1028, 8)); // Core 1 has no reservation

    const auto& res = mon.get_core_reservation(0);
    CHECK(res.valid);
    CHECK(res.granule_address == 0x1000);
    log_pass("D1: Exclusive Reservation Creation (LDXR)");
}

void test_d2_successful_store_exclusive() {
    log_test("D2: Successful Exclusive Store (STXR Success)");
    winios::SmpExclusiveMonitorManager mon(4);

    mon.load_exclusive(0, 0x2000, 8);
    bool write_executed = false;
    const bool success = mon.store_exclusive(0, 0x2000, 8, [&write_executed]() {
        write_executed = true;
    });

    CHECK(success);
    CHECK(write_executed);
    CHECK(!mon.is_reservation_valid(0, 0x2000, 8)); // Consumed
    CHECK(mon.get_telemetry().store_exclusive_success_count == 1);
    log_pass("D2: Successful Exclusive Store (STXR Success)");
}

void test_d3_cross_core_invalidation() {
    log_test("D3: Cross-Core Exclusive Reservation Invalidation");
    winios::SmpExclusiveMonitorManager mon(4);

    // Core 0 loads exclusive at 0x3010 (granule 0x3000)
    mon.load_exclusive(0, 0x3010, 8);
    CHECK(mon.is_reservation_valid(0, 0x3010, 8));

    // Core 1 writes to 0x3030 (same granule 0x3000)
    mon.invalidate_reservations(1, 0x3030, 8);

    // Core 0's reservation must now be invalid!
    CHECK(!mon.is_reservation_valid(0, 0x3010, 8));

    bool write_executed = false;
    const bool success = mon.store_exclusive(0, 0x3010, 8, [&write_executed]() {
        write_executed = true;
    });

    CHECK(!success);
    CHECK(!write_executed);
    CHECK(mon.get_telemetry().store_exclusive_fail_count == 1);
    CHECK(mon.get_telemetry().invalidation_count == 1);
    log_pass("D3: Cross-Core Exclusive Reservation Invalidation");
}

void test_d4_non_overlapping_independence() {
    log_test("D4: Non-Overlapping Granule Independence");
    winios::SmpExclusiveMonitorManager mon(4);

    // Core 0 loads exclusive at 0x4000 (granule 0x4000)
    mon.load_exclusive(0, 0x4000, 8);

    // Core 1 writes to 0x5000 (granule 0x5000)
    mon.invalidate_reservations(1, 0x5000, 8);

    // Core 0's reservation MUST still be valid!
    CHECK(mon.is_reservation_valid(0, 0x4000, 8));

    bool write_executed = false;
    const bool success = mon.store_exclusive(0, 0x4000, 8, [&write_executed]() {
        write_executed = true;
    });
    CHECK(success);
    CHECK(write_executed);
    log_pass("D4: Non-Overlapping Granule Independence");
}

void test_d5_explicit_clrex() {
    log_test("D5: Explicit CLREX Reservation Clear");
    winios::SmpExclusiveMonitorManager mon(4);

    mon.load_exclusive(2, 0x6000, 8);
    CHECK(mon.is_reservation_valid(2, 0x6000, 8));

    mon.clear_reservation(2);
    CHECK(!mon.is_reservation_valid(2, 0x6000, 8));
    CHECK(mon.get_telemetry().clrex_count == 1);
    log_pass("D5: Explicit CLREX Reservation Clear");
}

void test_d6_telemetry() {
    log_test("D6: Exclusive Monitor Telemetry Accounting");
    winios::SmpExclusiveMonitorManager mon(4);

    mon.load_exclusive(0, 0x7000, 8);
    mon.clear_reservation(0);
    mon.load_exclusive(1, 0x7000, 8);
    mon.store_exclusive(1, 0x7000, 8, nullptr);
    mon.store_exclusive(1, 0x7000, 8, nullptr); // Fails

    const auto t = mon.get_telemetry();
    CHECK(t.load_exclusive_count == 2);
    CHECK(t.clrex_count == 1);
    CHECK(t.store_exclusive_success_count == 1);
    CHECK(t.store_exclusive_fail_count == 1);
    log_pass("D6: Exclusive Monitor Telemetry Accounting");
}

// -----------------------------------------------------------------------------
// Group E: Cross-Core Interrupt Router Tests
// -----------------------------------------------------------------------------

void test_e1_per_core_targeting() {
    log_test("E1: Per-Core Interrupt Request Targeting");
    winios::SmpInterruptRouter router(4);

    router.request_interrupt(2, 45, 0x80, 0);
    CHECK(router.has_pending(2));
    CHECK(!router.has_pending(0));
    CHECK(!router.has_pending(1));
    CHECK(!router.has_pending(3));
    CHECK(router.pending_count(2) == 1);
    log_pass("E1: Per-Core Interrupt Request Targeting");
}

void test_e2_classification() {
    log_test("E2: GICv3 SGI / PPI / SPI Classification");
    CHECK(winios::classify_interrupt(5) == winios::InterruptType::SGI);
    CHECK(winios::classify_interrupt(15) == winios::InterruptType::SGI);
    CHECK(winios::classify_interrupt(16) == winios::InterruptType::PPI);
    CHECK(winios::classify_interrupt(27) == winios::InterruptType::PPI);
    CHECK(winios::classify_interrupt(30) == winios::InterruptType::PPI);
    CHECK(winios::classify_interrupt(32) == winios::InterruptType::SPI);
    CHECK(winios::classify_interrupt(100) == winios::InterruptType::SPI);
    log_pass("E2: GICv3 SGI / PPI / SPI Classification");
}

void test_e3_request_delivery_separation() {
    log_test("E3: Strict Request vs Delivery Separation");
    winios::SmpInterruptRouter router(4);
    winios::CPU cpu;
    cpu.reset();

    // Enqueue request
    router.request_interrupt(0, 32, 0x80, 1);
    CHECK(router.has_pending(0));

    // Request is queued, but exception not yet taken
    CHECK(!cpu.exception_taken_in_step());

    // Delivery phase commits exception
    const bool delivered = router.deliver_next(0, cpu);
    CHECK(delivered);
    CHECK(!router.has_pending(0));
    log_pass("E3: Strict Request vs Delivery Separation");
}

void test_e4_fifo_ordering() {
    log_test("E4: Deterministic FIFO Delivery Ordering");
    winios::SmpInterruptRouter router(4);
    router.request_interrupt(0, 50, 0x80, 1);
    router.request_interrupt(0, 51, 0x80, 1);
    router.request_interrupt(0, 52, 0x80, 1);

    auto p1 = router.peek_pending(0);
    CHECK(p1.has_value() && p1->intid == 50);

    winios::CPU cpu;
    cpu.reset();
    router.deliver_next(0, cpu);

    auto p2 = router.peek_pending(0);
    CHECK(p2.has_value() && p2->intid == 51);
    router.deliver_next(0, cpu);

    auto p3 = router.peek_pending(0);
    CHECK(p3.has_value() && p3->intid == 52);
    router.deliver_next(0, cpu);

    CHECK(!router.has_pending(0));
    log_pass("E4: Deterministic FIFO Delivery Ordering");
}

void test_e5_quiescent_wakeup() {
    log_test("E5: Awakening Quiescent Core from WFI");
    winios::SmpInterruptRouter router(4);
    winios::CPU cpu;
    cpu.reset();
    cpu.set_waiting_for_interrupt(true);
    CHECK(cpu.is_waiting_for_interrupt());

    router.request_interrupt(0, 27, 0x80, 0);
    router.deliver_next(0, cpu);

    CHECK(!cpu.is_waiting_for_interrupt());
    CHECK(!cpu.is_waiting());
    log_pass("E5: Awakening Quiescent Core from WFI");
}

void test_e6_sgi_multicast() {
    log_test("E6: Cross-Core SGI Multicast Routing");
    winios::SmpInterruptRouter router(4);

    // Core 0 sends SGI 7 to Core 1 (bit 1) and Core 3 (bit 3): mask = 0b1010 = 0xA
    router.request_sgi(0, 0xAu, 7, 0x80);

    CHECK(!router.has_pending(0));
    CHECK(router.has_pending(1));
    CHECK(!router.has_pending(2));
    CHECK(router.has_pending(3));

    auto r1 = router.peek_pending(1);
    CHECK(r1.has_value() && r1->intid == 7 && r1->type == winios::InterruptType::SGI);

    auto r3 = router.peek_pending(3);
    CHECK(r3.has_value() && r3->intid == 7 && r3->type == winios::InterruptType::SGI);
    log_pass("E6: Cross-Core SGI Multicast Routing");
}

// -----------------------------------------------------------------------------
// Group F: Integration & Replay Protection Boundaries
// -----------------------------------------------------------------------------

void test_f1_single_core_replay_compatibility() {
    log_test("F1: Single-Core Replay Session Preserved");
    winios::replay::ReplayConfig cfg{};
    cfg.mode = winios::replay::ReplayMode::SingleCore;
    cfg.allow_experimental_smp = false;

    winios::replay::ReplaySession session;
    CHECK(session.state() == winios::replay::ReplayLifecycleState::Idle);
    log_pass("F1: Single-Core Replay Session Preserved");
}

void test_f2_mandatory_smp_replay_rejection() {
    log_test("F2: Mandatory SMP Replay Engine Rejection (UnsupportedConfiguration)");
    winios::replay::ReplayConfig cfg{};
    cfg.mode = winios::replay::ReplayMode::MultiCore;
    cfg.allow_experimental_smp = false;

    winios::replay::ReplaySession session;
    const auto res = session.prepare("dummy.wsnap", "dummy.sndl", cfg);
    CHECK(res.is_failure());
    CHECK(res.code == winios::replay::ReplayErrorCode::UnsupportedConfiguration);
    CHECK(session.state() == winios::replay::ReplayLifecycleState::Failed);
    CHECK(session.stats().completion_status == winios::replay::ReplayCompletionStatus::UnsupportedConfiguration);
    log_pass("F2: Mandatory SMP Replay Engine Rejection (UnsupportedConfiguration)");
}

void test_f3_format_compatibility() {
    log_test("F3: Architecture and Format Compatibility (.wsnap, .sndl, .wtr)");
    CHECK(winios::snapshot::kWSnapMagic[0] == 'W');
    CHECK(winios::snapshot::kWSnapMagic[1] == 'S');
    CHECK(winios::snapshot::kWSnapCurrentVersion == 1u);
    CHECK(winios::sndl::kWSndlMagic[0] == 'W');
    CHECK(winios::sndl::kWSndlMagic[1] == 'S');
    CHECK(winios::sndl::kWSndlCurrentVersion == 1u);
    CHECK(sizeof(winios::trace::TraceEvent) == 64);
    log_pass("F3: Architecture and Format Compatibility (.wsnap, .sndl, .wtr)");
}

// -----------------------------------------------------------------------------
// Group G: Deterministic Multicore Scheduler Stress Tests
// -----------------------------------------------------------------------------

void test_g1_2_core_stress() {
    log_test("G1: 2-Core Deterministic Interleaved Stress (10 Repetitions)");
    std::uint64_t expected_hash = 0;

    for (int rep = 0; rep < 10; ++rep) {
        auto mem = std::make_shared<winios::SmpSharedMemory>(131072, 2);
        winios::SmpScheduler sched(mem, 2, 100);

        // Core 0 executes loop at 0x0000
        for (std::size_t i = 0; i < 200; ++i) {
            mem->write_u32(0, i * 4, 0x91000400u); // ADD X0, X0, #1
        }
        mem->write_u32(0, 200 * 4, 0xD4400000u); // HLT

        // Core 1 executes loop at 0x4000
        for (std::size_t i = 0; i < 200; ++i) {
            mem->write_u32(1, 0x4000 + i * 4, 0x91000821u); // ADD X1, X1, #2
        }
        mem->write_u32(1, 0x4000 + 200 * 4, 0xD4400000u); // HLT

        auto cpu0 = std::make_shared<winios::CPU>();
        auto cpu1 = std::make_shared<winios::CPU>();
        cpu0->set_program_counter(0x0000);
        cpu1->set_program_counter(0x4000);

        sched.register_core(0, cpu0, winios::CoreLifecycleState::Runnable);
        sched.register_core(1, cpu1, winios::CoreLifecycleState::Runnable);

        sched.run_deterministic(100000);
        const std::uint64_t hash = sched.compute_canonical_multicore_hash();

        if (rep == 0) {
            expected_hash = hash;
        } else {
            CHECK(hash == expected_hash);
        }
    }
    log_pass("G1: 2-Core Deterministic Interleaved Stress (10 Repetitions)");
}

void test_g2_4_core_stress() {
    log_test("G2: 4-Core Deterministic Interleaved Stress (10 Repetitions)");
    std::uint64_t expected_hash = 0;

    for (int rep = 0; rep < 10; ++rep) {
        auto mem = std::make_shared<winios::SmpSharedMemory>(262144, 4);
        winios::SmpScheduler sched(mem, 4, 50);

        std::vector<std::shared_ptr<winios::CPU>> cpus;
        for (std::size_t c = 0; c < 4; ++c) {
            const std::uint64_t base = c * 0x2000;
            for (std::size_t i = 0; i < 100; ++i) {
                mem->write_u32(c, base + i * 4, 0x91000400u + static_cast<std::uint32_t>(c)); // ADD
            }
            mem->write_u32(c, base + 100 * 4, 0xD4400000u); // HLT

            auto cpu = std::make_shared<winios::CPU>();
            cpu->set_program_counter(base);
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
            cpus.push_back(cpu);
        }

        sched.run_deterministic(100000);
        const std::uint64_t hash = sched.compute_canonical_multicore_hash();

        if (rep == 0) {
            expected_hash = hash;
        } else {
            CHECK(hash == expected_hash);
        }
    }
    log_pass("G2: 4-Core Deterministic Interleaved Stress (10 Repetitions)");
}

void test_g3_8_core_stress() {
    log_test("G3: 8-Core Deterministic Interleaved Stress (10 Repetitions)");
    std::uint64_t expected_hash = 0;

    for (int rep = 0; rep < 10; ++rep) {
        auto mem = std::make_shared<winios::SmpSharedMemory>(524288, 8);
        winios::SmpScheduler sched(mem, 8, 25);

        for (std::size_t c = 0; c < 8; ++c) {
            const std::uint64_t base = c * 0x2000;
            for (std::size_t i = 0; i < 50; ++i) {
                mem->write_u32(c, base + i * 4, 0x91000400u + static_cast<std::uint32_t>(c));
            }
            mem->write_u32(c, base + 50 * 4, 0xD4400000u); // HLT

            auto cpu = std::make_shared<winios::CPU>();
            cpu->set_program_counter(base);
            sched.register_core(c, cpu, winios::CoreLifecycleState::Runnable);
        }

        sched.run_deterministic(100000);
        const std::uint64_t hash = sched.compute_canonical_multicore_hash();

        if (rep == 0) {
            expected_hash = hash;
        } else {
            CHECK(hash == expected_hash);
        }
    }
    log_pass("G3: 8-Core Deterministic Interleaved Stress (10 Repetitions)");
}

} // namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    try {
    std::cout << "========================================================\n";
    std::cout << "  WinIOS Phase 52 Stage 22: SMP Foundation Unit Tests\n";
    std::cout << "========================================================\n\n";

    // Group A
    test_a1_independent_register_files();
    test_a2_independent_pc_and_sp();
    test_a3_independent_pstate_nzcv_daif();
    test_a4_independent_simd_fp();
    test_a5_independent_lifecycle_and_retirement();
    test_a6_capture_and_apply();
    test_a7_canonical_state_hash();

    // Group B
    test_b1_cross_core_visibility();
    test_b2_address_identity();
    test_b3_multi_width_accesses();
    test_b4_per_core_telemetry();
    test_b5_out_of_bounds();

    // Group C
    test_c1_core_registration();
    test_c2_round_robin_selection();
    test_c3_quantum_enforcement();
    test_c4_truncation_on_hlt();
    test_c5_truncation_on_wfi();
    test_c6_quiescent_skipping();
    test_c7_monotonic_global_sequence();
    test_c8_repeatability_10_runs();

    // Group D
    test_d1_reservation_creation();
    test_d2_successful_store_exclusive();
    test_d3_cross_core_invalidation();
    test_d4_non_overlapping_independence();
    test_d5_explicit_clrex();
    test_d6_telemetry();

    // Group E
    test_e1_per_core_targeting();
    test_e2_classification();
    test_e3_request_delivery_separation();
    test_e4_fifo_ordering();
    test_e5_quiescent_wakeup();
    test_e6_sgi_multicast();

    // Group F
    test_f1_single_core_replay_compatibility();
    test_f2_mandatory_smp_replay_rejection();
    test_f3_format_compatibility();

    // Group G
    test_g1_2_core_stress();
    test_g2_4_core_stress();
    test_g3_8_core_stress();

    std::cout << "\n========================================================\n";
    std::cout << "  ALL 7 GROUPS (34 TESTS) PASSED SUCCESSFULLY!\n";
    std::cout << "========================================================\n";
    return 0;
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION: " << e.what() << "\n";
        return 1;
    }
}
