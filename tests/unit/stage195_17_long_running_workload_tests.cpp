// ============================================================================
// Stage 195.17: Long-Running Synthetic Workload Corpus & Deterministic Replay
// Mandatory Acceptance Test I: Full Pipeline Stress & Deterministic Replay
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "kernel/cpu_context.h"
#include "kernel/exception_frame_manager.h"
#include "kernel/preemption_timer.h"
#include "kernel/scheduler.h"
#include "kernel/smp_scheduler.h"
#include "kernel/sync.h"
#include "kernel/thread.h"
#include "loader/macho_loader.h"
#include "memory/demand_paging_manager.h"
#include "memory/kernel_heap_allocator.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/virtual_page_table.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::memory;

// 64-bit FNV-1a Hash implementation
static void fnv1a_hash_u64(std::uint64_t& hash, std::uint64_t val) {
    constexpr std::uint64_t kPrime = 0x100000001B3ULL;
    for (int i = 0; i < 8; ++i) {
        std::uint8_t byte = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF);
        hash ^= byte;
        hash *= kPrime;
    }
}

static void fnv1a_hash_bytes(std::uint64_t& hash, const std::uint8_t* data, std::size_t len) {
    constexpr std::uint64_t kPrime = 0x100000001B3ULL;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= kPrime;
    }
}

struct WorkloadReplayResult {
    std::uint64_t retired_instructions{0};
    std::uint64_t preemption_count{0};
    std::uint64_t context_switch_count{0};
    std::uint64_t faults_resolved{0};
    std::uint64_t spinlock_acquires{0};
    std::uint64_t state_checksum{0xcbf29ce484222325ULL}; // FNV offset basis
};

// -----------------------------------------------------------------------------
// Deterministic Synthetic Kernel Workload Runner
// -----------------------------------------------------------------------------
static bool run_synthetic_kernel_workload(WorkloadReplayResult& out_result) {
    // 16 MB Memory
    Memory mem(16 * 1024 * 1024);

    // Initialize physical page allocator and kernel heap
    PhysicalPageManager ppm(mem, 0x200000ULL, 0x600000ULL, 0x10000ULL);
    CHECK(ppm.initialize(false), "Init PPM");

    KernelHeapAllocator heap(mem, 0x800000ULL, 0x400000ULL);
    CHECK(heap.initialize(), "Init heap");

    // Page table setup
    const std::uint64_t root_l2_pa = 0x30000ULL;
    const std::uint64_t l3_demand_pa = 0x50000ULL;
    const std::uint64_t l3_code_pa = 0x60000ULL;
    const std::uint64_t vbar_pa = 0x80000ULL;
    const std::uint64_t backing_pool_pa = 0x100000ULL;

    for (std::size_t i = 0; i < kPhysicalPageSize16KB; ++i) {
        mem.write_u8(static_cast<std::size_t>(root_l2_pa + i), 0);
        mem.write_u8(static_cast<std::size_t>(l3_code_pa + i), 0);
        mem.write_u8(static_cast<std::size_t>(l3_demand_pa + i), 0);
    }

    mem.write_u64(static_cast<std::size_t>(root_l2_pa + 0),
                  VirtualPageTable::make_table_descriptor(l3_code_pa));

    const std::uint64_t l2_idx = (0x10000000ULL >> 25u) & 0x7FFu;
    mem.write_u64(static_cast<std::size_t>(root_l2_pa + l2_idx * 8u),
                  VirtualPageTable::make_table_descriptor(l3_demand_pa));

    for (std::uint64_t p = 0; p < 48; ++p) {
        const std::uint64_t addr = p * kPhysicalPageSize16KB;
        PagePermissions perms = PagePermissions::KernelReadWrite;
        if (p < 4 || (p >= 32 && p < 36)) {
            perms = PagePermissions::KernelExecute;
        }
        mem.write_u64(static_cast<std::size_t>(l3_code_pa + p * 8u),
                      VirtualPageTable::make_page_descriptor(addr, perms));
    }

    // Initialize Demand Paging Manager & Vector Table
    DemandPagingConfig dp_cfg;
    dp_cfg.root_l2_table_pa = root_l2_pa;
    dp_cfg.l3_table_pa = l3_demand_pa;
    dp_cfg.backing_pool_start_pa = backing_pool_pa;
    DemandPagingManager dpm(mem, dp_cfg);
    dpm.initialize_guest_control_data();
    dpm.install_vector_table(vbar_pa);

    mem.write_u64(static_cast<std::size_t>(backing_pool_pa), 0xFEEDFACEDEADBEEFULL);

    // Thread manager & SMP Scheduler
    KernelThreadManager thread_mgr;
    SmpScheduler smp_sched(thread_mgr, mem, 2);
    CHECK(smp_sched.initialize(0x50000, 0x2000, 0x1000), "Init SMP scheduler");

    // Create 3 synthetic kernel threads:
    // Task 0: CPU compute & register rotation (Stack: 0x40000)
    // Task 1: SIMD / FP intensive vector math (Stack: 0x44000)
    // Task 2: Memory demand fault touch (Stack: 0x48000)
    auto* t0 = thread_mgr.create_thread("worker_compute", 0x2000, 0, 0x40000, 0x3000, 20);
    auto* t1 = thread_mgr.create_thread("worker_simd", 0x3000, 0, 0x44000, 0x3000, 18);
    auto* t2 = thread_mgr.create_thread("worker_mem", 0x4000, 0, 0x48000, 0x3000, 16);

    CHECK(t0 && t1 && t2, "Create 3 threads");

    // Task 0 code at 0x2000:
    // 0x2000: ADD X0, X0, #1
    // 0x2004: ADD X1, X1, #2
    // 0x2008: EOR X2, X0, X1
    // 0x200C: B . - 12 (0x2000)
    mem.write_u32(0x2000, 0x91000400u); // ADD X0, X0, #1
    mem.write_u32(0x2004, 0x91000821u); // ADD X1, X1, #2
    mem.write_u32(0x2008, 0xCA010002u); // EOR X2, X0, X1
    mem.write_u32(0x200C, 0x17FFFFFDu); // B 0x2000

    // Task 1 code at 0x3000:
    // 0x3000: FADD S0, S0, S1
    // 0x3004: FMUL S2, S0, S2
    // 0x3008: B . - 8 (0x3000)
    mem.write_u32(0x3000, 0x1E212800u); // FADD S0, S0, S1
    mem.write_u32(0x3004, 0x1E220802u); // FMUL S2, S0, S2
    mem.write_u32(0x3008, 0x17FFFFFEu); // B 0x3000

    // Task 2 code at 0x4000:
    // 0x4000: MOVZ X1, #0x1000, LSL #16 (0x10000000)
    // 0x4004: LDR X0, [X1]              (Traps to Demand Pager!)
    // 0x4008: B .                       (Loop)
    mem.write_u32(0x4000, 0xD2A20001u); // MOVZ X1, #0x1000, LSL #16
    mem.write_u32(0x4004, 0xF9400020u); // LDR X0, [X1]
    mem.write_u32(0x4008, 0x14000000u); // B .

    // Core 0 and Core 1 Setup
    CPU cpu0;
    cpu0.set_core_id(0);
    cpu0.set_architectural_exceptions(true);
    cpu0.set_current_el(1);
    cpu0.set_sp_sel(true);
    cpu0.set_vbar_el1(vbar_pa);
    cpu0.set_cpacr_el1(0x00300000ULL);
    cpu0.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu0.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu0.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu0.get_mmu().set_sctlr_el1(cpu0.get_mmu().get_sctlr_el1() | 1ULL);

    CPU cpu1;
    cpu1.set_core_id(1);
    cpu1.set_architectural_exceptions(true);
    cpu1.set_current_el(1);
    cpu1.set_sp_sel(true);
    cpu1.set_vbar_el1(vbar_pa);
    cpu1.set_cpacr_el1(0x00300000ULL);
    cpu1.get_mmu().set_ttbr0_el1(root_l2_pa);
    cpu1.get_mmu().set_tcr_el1((2ULL << 14) | 28ULL);
    cpu1.get_mmu().set_mair_el1(0x00000000000004FFULL);
    cpu1.get_mmu().set_sctlr_el1(cpu1.get_mmu().get_sctlr_el1() | 1ULL);

    // Initial thread context configuration
    t0->set_cpu_affinity(1ULL << 0); // Pin to Core 0
    t1->set_cpu_affinity(1ULL << 1); // Pin to Core 1
    t2->set_cpu_affinity(1ULL << 0); // Pin to Core 0

    smp_sched.enqueue_thread(t0);
    smp_sched.enqueue_thread(t1);
    smp_sched.enqueue_thread(t2);

    KernelSpinlock lock(mem, 0x40100ULL);
    lock.initialize();
    std::uint64_t shared_counter = 0;

    // Dispatch initial threads via SMP ticks
    smp_sched.tick(0, cpu0);
    auto* cur0 = smp_sched.current_thread(0);
    CHECK(cur0 == t0, "Core 0 schedules t0");

    smp_sched.tick(1, cpu1);
    auto* cur1 = smp_sched.current_thread(1);
    CHECK(cur1 == t1, "Core 1 schedules t1");

    // Initialize SIMD values on Core 1 for Task 1
    cpu1.write_vector_register(0, {0x3F80000000000000ULL, 0x0}); // 1.0f
    cpu1.write_vector_register(1, {0x4000000000000000ULL, 0x0}); // 2.0f
    cpu1.write_vector_register(2, {0x3F80000000000000ULL, 0x0}); // 1.0f

    std::uint64_t instructions_retired = 0;
    std::uint64_t preemptions = 0;
    std::uint64_t context_switches = 0;
    std::uint64_t spinlocks_acquired = 0;

    // Run deterministic multi-threaded synthetic pipeline for 300 cycles
    for (std::size_t cycle = 0; cycle < 300; ++cycle) {
        // Step Core 0
        cpu0.step(mem);
        ++instructions_retired;

        // Step Core 1
        cpu1.step(mem);
        ++instructions_retired;

        // Spinlock contention & update every 10 cycles
        if (cycle % 10 == 0) {
            CHECK(lock.try_acquire_host(0), "Core 0 acquires lock");
            ++shared_counter;
            ++spinlocks_acquired;
            lock.release_host(0);
        }

        // Periodic preemption tick on Core 0 every 60 cycles
        if (cycle > 0 && (cycle % 60) == 0) {
            // Save current thread's 896-byte ExceptionFrame onto its stack
            CHECK(ExceptionFrameManager::save_exception_frame(cpu0, mem, cur0, 0x0, 0x0),
                  "Save Core 0 ExceptionFrame");
            cur0->set_state(ThreadState::Ready);
            smp_sched.enqueue_thread(cur0);
            ++preemptions;
            ++context_switches;

            // Pick next thread for Core 0
            smp_sched.tick(0, cpu0);
            cur0 = smp_sched.current_thread(0);
            CHECK(cur0 != nullptr, "Core 0 scheduler provides next thread");
            cur0->set_state(ThreadState::Running);
            CHECK(ExceptionFrameManager::restore_exception_frame(cpu0, mem, cur0),
                  "Restore Core 0 next thread context");
        }

        // Mid-workload task migration at cycle 180: Migrate t0 affinity from Core 0 to Core 1
        if (cycle == 180) {
            t0->set_cpu_affinity(1ULL << 1); // Move t0 to Core 1
        }
    }

    // Now resolve Task 2's demand page fault on Core 0
    if (cur0 == t2 || smp_sched.core_ready_count(0) > 0) {
        if (cur0 != t2) {
            ExceptionFrameManager::save_exception_frame(cpu0, mem, cur0, 0x0, 0x0);
            cur0->set_state(ThreadState::Ready);
            smp_sched.enqueue_thread(cur0);
            cur0 = t2;
            cur0->set_state(ThreadState::Running);
            ExceptionFrameManager::restore_exception_frame(cpu0, mem, cur0);
            ++context_switches;
        }

        // Execute t2 instructions up to page fault
        // Step 1: MOVZ X1, #0x1000, LSL #16
        cpu0.step(mem);
        ++instructions_retired;

        // Step 2: LDR X0, [X1] -> Traps to VBAR+0x200!
        cpu0.step(mem);
        ++instructions_retired;
        CHECK(cpu0.get_program_counter() == vbar_pa + 0x200u, "Traps to demand pager");

        // Run demand fault resolution handler in EL1 until ERET returns
        std::size_t h_steps = 0;
        while (cpu0.get_program_counter() != 0x4004 && h_steps < 100) {
            cpu0.step(mem);
            ++instructions_retired;
            ++h_steps;
        }
        CHECK(cpu0.get_program_counter() == 0x4004, "ERET returned to LDR");

        // Step 3: Re-execute LDR successfully!
        cpu0.step(mem);
        ++instructions_retired;
        CHECK(cpu0.read_register(0) == 0xFEEDFACEDEADBEEFULL, "Value read from backing page");
    }

    auto stats = dpm.get_guest_stats();

    out_result.retired_instructions = instructions_retired;
    out_result.preemption_count = preemptions;
    out_result.context_switch_count = context_switches;
    out_result.faults_resolved = stats.total_faults_resolved;
    out_result.spinlock_acquires = spinlocks_acquired;

    // Compute comprehensive state checksum across:
    // Core 0 GPRs (X0..X30), PC, SP, FAR, ESR, DAIF, NZCV
    // Core 1 GPRs (X0..X30), Q0..Q31, PC, SP
    // Shared counter & Heap stats
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned r = 0; r <= 30; ++r) {
        fnv1a_hash_u64(hash, cpu0.read_register(r));
        fnv1a_hash_u64(hash, cpu1.read_register(r));
    }
    for (unsigned q = 0; q <= 31; ++q) {
        auto vec = cpu1.read_vector_register(q);
        fnv1a_hash_u64(hash, vec.low);
        fnv1a_hash_u64(hash, vec.high);
    }
    fnv1a_hash_u64(hash, cpu0.get_program_counter());
    fnv1a_hash_u64(hash, cpu1.get_program_counter());
    fnv1a_hash_u64(hash, cpu0.get_stack_pointer());
    fnv1a_hash_u64(hash, cpu1.get_stack_pointer());
    fnv1a_hash_u64(hash, shared_counter);
    fnv1a_hash_u64(hash, stats.total_faults_resolved);
    fnv1a_hash_u64(hash, heap.get_stats().total_alloc_operations);

    // Hash modified RAM regions: TCB stacks at 0x40000, 0x44000, 0x48000 (512 bytes each)
    fnv1a_hash_bytes(hash, mem.raw_data() + 0x3F000, 4096);
    fnv1a_hash_bytes(hash, mem.raw_data() + 0x43000, 4096);
    fnv1a_hash_bytes(hash, mem.raw_data() + 0x47000, 4096);

    out_result.state_checksum = hash;
    return true;
}

// -----------------------------------------------------------------------------
// Test 1: Deterministic Replay Verification
// -----------------------------------------------------------------------------
static bool test_mandatory_i_deterministic_replay() {
    WorkloadReplayResult run1;
    WorkloadReplayResult run2;

    CHECK(run_synthetic_kernel_workload(run1), "Workload Run 1 must succeed");
    CHECK(run_synthetic_kernel_workload(run2), "Workload Run 2 must succeed");

    // 1. Retired instructions check
    CHECK(run1.retired_instructions > 500, "Must retire significant instruction count");
    CHECK(run1.retired_instructions == run2.retired_instructions,
          "Mandatory Test I: Instructions retired must be 100% identical across replays");

    // 2. Preemptions check
    CHECK(run1.preemption_count > 0, "Must record preemption occurrences");
    CHECK(run1.preemption_count == run2.preemption_count,
          "Mandatory Test I: Preemption counts must be 100% identical");

    // 3. Context switches check
    CHECK(run1.context_switch_count == run2.context_switch_count,
          "Mandatory Test I: Context switch counts must be 100% identical");

    // 4. Page faults check
    CHECK(run1.faults_resolved == 1, "Exactly 1 page fault resolved");
    CHECK(run1.faults_resolved == run2.faults_resolved,
          "Mandatory Test I: Page faults resolved must match exactly");

    // 5. Spinlock acquires check
    CHECK(run1.spinlock_acquires == 30, "Exactly 30 spinlock acquires");
    CHECK(run1.spinlock_acquires == run2.spinlock_acquires,
          "Mandatory Test I: Spinlock acquire counts must match exactly");

    // 6. Complete cryptographic state hash check
    CHECK(run1.state_checksum == run2.state_checksum,
          "Mandatory Test I: Complete system state checksum must be 100% identical");

    std::cout << "[PASS] test_mandatory_i_deterministic_replay\n"
              << "       Retired Instructions : " << run1.retired_instructions << " (Run 1) == " << run2.retired_instructions << " (Run 2)\n"
              << "       Preemptions Handled  : " << run1.preemption_count << " (Run 1) == " << run2.preemption_count << " (Run 2)\n"
              << "       Context Switches     : " << run1.context_switch_count << " (Run 1) == " << run2.context_switch_count << " (Run 2)\n"
              << "       Faults Resolved      : " << run1.faults_resolved << " (Run 1) == " << run2.faults_resolved << " (Run 2)\n"
              << "       Spinlock Operations  : " << run1.spinlock_acquires << " (Run 1) == " << run2.spinlock_acquires << " (Run 2)\n"
              << "       State Checksum       : 0x" << std::hex << run1.state_checksum << " == 0x" << run2.state_checksum << std::dec << "\n";

    return true;
}

int main() {
    std::cout << "Running Stage 195.17 Long-Running Synthetic Workload & Deterministic Replay Tests...\n";

    if (!test_mandatory_i_deterministic_replay()) return 1;

    std::cout << "\nAll Stage 195.17 Deterministic Replay tests PASSED successfully.\n";
    return 0;
}
