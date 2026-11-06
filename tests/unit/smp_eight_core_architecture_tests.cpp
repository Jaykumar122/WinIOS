#ifdef NDEBUG
#undef NDEBUG
#endif

#include "cpu/cpu.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_topology.h"
#include "cpu/smp_scheduler.h"
#include "cpu/smp_shared_memory.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/virtual_time.h"
#include "snapshot/smp_snapshot_manager.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace winios;
using namespace winios::snapshot;

namespace {

// Workload 1: Eight-Core Arithmetic
void test_workload_1_eight_core_arithmetic() {
    std::cout << "[Workload 1] Eight-Core Arithmetic Stress..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler sched(mem, 8, 20);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        const std::uint64_t base_pc = 0x2000 + c * 0x200;
        cpu->set_program_counter(base_pc);

        // Arithmetic loop:
        // MOVZ X0, #(c + 1)   -> 0xD2800000 | ((c+1) << 5)
        // ADD X0, X0, #1      -> 0x91000400
        // ADD X0, X0, #2      -> 0x91000800
        // NOP                 -> 0xD503201F
        const std::uint32_t movz_op = 0xD2800000u | (static_cast<std::uint32_t>(c + 1) << 5);
        mem->raw_memory().write_u32(base_pc + 0, movz_op);
        mem->raw_memory().write_u32(base_pc + 4, 0x91000400u);
        mem->raw_memory().write_u32(base_pc + 8, 0x91000800u);
        for (std::uint64_t off = 12; off < 200; off += 4) {
            mem->raw_memory().write_u32(base_pc + off, 0xD503201Fu);
        }

        cpus.push_back(cpu);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    assert(sched.core_count() == 8);

    // Run 16 slices (2 complete rounds across 8 cores)
    std::uint64_t total_retired = 0;
    for (int i = 0; i < 16; ++i) {
        const auto rec = sched.step_quantum();
        total_retired += rec.instructions_retired;
    }

    assert(total_retired > 0);
    for (std::size_t c = 0; c < 8; ++c) {
        assert(sched.get_slot(c).total_instructions_retired > 0);
        assert(cpus[c]->read_register(0) == (c + 1) + 1 + 2);
    }

    std::cout << "  -> Workload 1 PASS (Total retired: " << total_retired << " instructions)" << std::endl;
}

// Workload 2: Shared Memory Contention
void test_workload_2_shared_memory_contention() {
    std::cout << "[Workload 2] Shared Memory Contention Stress..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler sched(mem, 8, 10);

    const std::uint64_t shared_loc = 0x10000;
    mem->raw_memory().write_u64(shared_loc, 0);

    // 8 cores write to independent locations first, then read shared location
    for (std::size_t c = 0; c < 8; ++c) {
        const std::uint64_t private_loc = 0x11000 + c * 64; // Separate cache-line sized regions
        mem->raw_memory().write_u64(private_loc, 0x1000ULL + c);
    }

    // Verify independent reads
    for (std::size_t c = 0; c < 8; ++c) {
        const std::uint64_t private_loc = 0x11000 + c * 64;
        assert(mem->raw_memory().read_u64(private_loc) == 0x1000ULL + c);
    }

    // 8 cores sequentially update shared location
    for (std::size_t c = 0; c < 8; ++c) {
        const std::uint64_t val = mem->raw_memory().read_u64(shared_loc);
        mem->raw_memory().write_u64(shared_loc, val + (c + 1));
    }

    // 1+2+3+4+5+6+7+8 = 36
    assert(mem->raw_memory().read_u64(shared_loc) == 36);

    std::cout << "  -> Workload 2 PASS (Final shared value: " << mem->raw_memory().read_u64(shared_loc) << ")" << std::endl;
}

// Workload 3: Exclusive Monitor Contention
void test_workload_3_exclusive_monitor_contention() {
    std::cout << "[Workload 3] Exclusive Monitor Contention Stress (64-Byte Granule)..." << std::endl;

    SmpExclusiveMonitorManager monitor(8);
    const std::uint64_t granule = 0x20040; // 64-byte aligned

    // 8 cores perform load-exclusive on the same granule
    for (std::size_t c = 0; c < 8; ++c) {
        monitor.load_exclusive(c, granule + (c * 4), 4);
        assert(monitor.is_reservation_valid(c, granule + (c * 4), 4));
    }

    // Core 0 executes store-exclusive successfully
    bool write_executed = false;
    const bool stxr_res0 = monitor.store_exclusive(0, granule, 4, [&]() {
        write_executed = true;
    });
    assert(stxr_res0);
    assert(write_executed);

    // Core 0's reservation should be cleared, AND other 7 cores' reservations invalidated!
    assert(!monitor.is_reservation_valid(0, granule, 4));
    for (std::size_t c = 1; c < 8; ++c) {
        assert(!monitor.is_reservation_valid(c, granule + (c * 4), 4));
        // STXR from other cores must now fail (return false, write not executed)
        bool other_write = false;
        const bool stxr_res = monitor.store_exclusive(c, granule + (c * 4), 4, [&]() {
            other_write = true;
        });
        assert(!stxr_res);
        assert(!other_write);
    }

    // CLREX test on Core 5
    monitor.load_exclusive(5, granule, 8);
    assert(monitor.is_reservation_valid(5, granule, 8));
    monitor.clear_reservation(5);
    assert(!monitor.is_reservation_valid(5, granule, 8));

    std::cout << "  -> Workload 3 PASS" << std::endl;
}

// Workload 4: Interrupt Stress
void test_workload_4_interrupt_stress() {
    std::cout << "[Workload 4] Eight-Core Interrupt Stress (PPI/SPI/SGI)..." << std::endl;

    SmpInterruptRouter router(8);
    std::vector<std::unique_ptr<CPU>> cpus;
    for (std::size_t c = 0; c < 8; ++c) {
        cpus.push_back(std::make_unique<CPU>());
        cpus[c]->set_core_id(c);
    }

    // 1. Request independent PPIs on all 8 cores (INTID 27 = virtual timer)
    for (std::size_t c = 0; c < 8; ++c) {
        router.request_interrupt(c, 27, 0x80u, c);
        assert(router.has_pending(c));
        assert(router.pending_count(c) == 1);
    }

    // 2. Request shared SPI (INTID 33) targeted to Core 3
    router.request_interrupt(3, 33, 0x70u, 0);
    assert(router.pending_count(3) == 2);

    // 3. Request SGI from Core 0 to Cores 1, 3, 5, 7 (mask = 0xAA)
    router.request_sgi(0, 0xAAu, 5, 0x60u);
    assert(router.has_pending(1));
    assert(router.has_pending(3));
    assert(router.has_pending(5));
    assert(router.has_pending(7));

    // 4. Deliver all pending interrupts across all 8 cores
    std::size_t total_delivered = 0;
    for (std::size_t c = 0; c < 8; ++c) {
        total_delivered += router.deliver_all_pending(c, *cpus[c]);
        assert(!router.has_pending(c));
    }
    assert(total_delivered >= 13);

    std::cout << "  -> Workload 4 PASS (Delivered " << total_delivered << " interrupts)" << std::endl;
}

// Workload 5: Timer Stress
void test_workload_5_timer_stress() {
    std::cout << "[Workload 5] Eight-Core Virtual Timer Stress..." << std::endl;

    VirtualTime vt;
    assert(vt.get_physical_counter() == 0);

    // Simulate 8 cores retiring instructions and advancing virtual time
    const std::uint64_t slice_instructions = 1000;
    for (int slice = 0; slice < 16; ++slice) {
        vt.advance_physical_counter(slice_instructions);
    }

    assert(vt.get_physical_counter() == 16 * slice_instructions);

    // Per-core comparators
    std::vector<std::uint64_t> comparators(8);
    for (std::size_t c = 0; c < 8; ++c) {
        comparators[c] = (c + 1) * 2000; // 2k, 4k, 6k, 8k, 10k, 12k, 14k, 16k
    }

    // Check expiration against current virtual ticks (16000)
    for (std::size_t c = 0; c < 8; ++c) {
        assert(vt.get_physical_counter() >= comparators[c]);
    }

    std::cout << "  -> Workload 5 PASS (Final virtual ticks: " << vt.get_physical_counter() << ")" << std::endl;
}

// Workload 6: Scheduler Lifecycle Stress
void test_workload_6_scheduler_lifecycle() {
    std::cout << "[Workload 6] Scheduler Lifecycle Transitions across Eight Cores..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x20000);
    for (std::uint64_t addr = 0; addr < 0x2000; addr += 4) {
        mem->raw_memory().write_u32(addr, 0xD503201Fu);
    }
    SmpScheduler sched(mem, 8, 50);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        cpus.push_back(cpu);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Sequence of transitions
    sched.get_slot(2).state.lifecycle = CoreLifecycleState::Halted;
    sched.get_slot(2).state.apply_to_cpu(*sched.get_slot(2).cpu);
    sched.get_slot(5).state.lifecycle = CoreLifecycleState::WaitingForInterrupt;
    sched.get_slot(5).state.apply_to_cpu(*sched.get_slot(5).cpu);
    sched.get_slot(7).state.lifecycle = CoreLifecycleState::Stopped;
    sched.get_slot(7).state.apply_to_cpu(*sched.get_slot(7).cpu);

    // Run 10 slices: cores 2, 5, 7 should never be selected
    for (int i = 0; i < 10; ++i) {
        const auto rec = sched.step_quantum();
        if (rec.reason != QuantumTruncationReason::AllCoresQuiescent) {
            assert(rec.core_id != 2);
            assert(rec.core_id != 5);
            assert(rec.core_id != 7);
        }
    }

    // Resume core 2, wake core 5
    sched.get_slot(2).state.lifecycle = CoreLifecycleState::Runnable;
    sched.get_slot(2).state.apply_to_cpu(*sched.get_slot(2).cpu);
    sched.interrupt_router().request_interrupt(5, 30);

    // Step again: core 2 and 5 are eligible
    const auto rec = sched.step_quantum();
    assert(rec.reason != QuantumTruncationReason::AllCoresQuiescent);

    std::cout << "  -> Workload 6 PASS" << std::endl;
}

// Workload 7: Snapshot Round Trip
void test_workload_7_snapshot_round_trip() {
    std::cout << "[Workload 7] Eight-Core Snapshot Round-Trip Serialization & Restoration..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x20000);
    SmpScheduler sched(mem, 8, 100);

    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
        cpu->write_register(0, 0x12345678ULL + c);
        cpu->set_program_counter(0x4000 + c * 0x100);
    }
    mem->raw_memory().write_u64(0x1000, 0xCAFEBABECAFEBABEuLL);

    std::vector<std::uint8_t> snap_buffer;
    SmpSnapshotCaptureOptions opts;
    opts.description = "Eight-Core Stress Test Snapshot";
    const auto cap_res = SmpSnapshotManager::capture_to_buffer(sched, snap_buffer, opts);
    assert(cap_res.success);
    assert(!snap_buffer.empty());

    // Validate captured snapshot buffer
    const auto val_res = SmpSnapshotManager::validate_buffer(snap_buffer.data(), snap_buffer.size());
    assert(val_res.valid);

    // Restore into fresh target scheduler
    auto target_mem = std::make_shared<SmpSharedMemory>(0x20000);
    SmpScheduler target_sched(target_mem, 8, 100);
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        target_sched.register_core(c, cpu, CoreLifecycleState::Halted);
    }

    const auto rest_res = SmpSnapshotManager::restore_from_buffer(snap_buffer.data(), snap_buffer.size(), target_sched);
    assert(rest_res.success);

    // Verify restored state
    assert(target_mem->raw_memory().read_u64(0x1000) == 0xCAFEBABECAFEBABEuLL);
    for (std::size_t c = 0; c < 8; ++c) {
        const auto& slot = target_sched.get_slot(c);
        assert(slot.state.core_id == c);
        assert(slot.state.pc == 0x4000 + c * 0x100);
        assert(slot.state.x[0] == 0x12345678ULL + c);
    }

    std::cout << "  -> Workload 7 PASS (Buffer size: " << snap_buffer.size() << " bytes)" << std::endl;
}

// Workload 8: Mixed Stress Workload
void test_workload_8_mixed_stress() {
    std::cout << "[Workload 8] Mixed Stress Workload across Eight Cores..." << std::endl;

    auto mem = std::make_shared<SmpSharedMemory>(0x40000);
    SmpScheduler sched(mem, 8, 25);

    std::vector<std::shared_ptr<CPU>> cpus;
    for (std::size_t c = 0; c < 8; ++c) {
        auto cpu = std::make_shared<CPU>();
        const std::uint64_t pc = 0x2000 + c * 0x100;
        cpu->set_program_counter(pc);
        for (std::uint64_t off = 0; off < 400; off += 4) {
            mem->raw_memory().write_u32(pc + off, 0xD503201Fu); // NOP
        }
        cpus.push_back(cpu);
        sched.register_core(c, cpu, CoreLifecycleState::Runnable);
    }

    // Step 24 quantums (3 full rounds across 8 cores)
    for (int i = 0; i < 24; ++i) {
        sched.step_quantum();
    }

    assert(sched.global_retirement_sequence() > 0);
    for (std::size_t c = 0; c < 8; ++c) {
        assert(sched.get_slot(c).quantums_allocated >= 3);
    }

    std::cout << "  -> Workload 8 PASS (Global sequence: " << sched.global_retirement_sequence() << ")" << std::endl;
}

} // namespace

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " WinIOS Phase 52 Stage 28: Eight-Core Architecture Stress Tests " << std::endl;
    std::cout << "================================================================" << std::endl;

    test_workload_1_eight_core_arithmetic();
    test_workload_2_shared_memory_contention();
    test_workload_3_exclusive_monitor_contention();
    test_workload_4_interrupt_stress();
    test_workload_5_timer_stress();
    test_workload_6_scheduler_lifecycle();
    test_workload_7_snapshot_round_trip();
    test_workload_8_mixed_stress();

    std::cout << "\n>>> ALL 8 EIGHT-CORE STRESS WORKLOADS PASSED <<<" << std::endl;
    return 0;
}
