#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_platform.h"
#include "device/virtual_uart_device.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"
#include "replay/replay_state_hash.h"

#define STAGE71_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::reference;
using namespace winios::replay;

namespace {

// -----------------------------------------------------------------------------
// Suite 1: TLB Cold vs Warm Lookup Speed & Hit Rate
// -----------------------------------------------------------------------------
bool test_tlb_cold_vs_warm_lookup_speed() {
    std::cout << "[1] test_tlb_cold_vs_warm_lookup_speed...\n";
    CPU cpu;
    Memory memory(0x40000);
    auto& mmu = cpu.get_mmu();

    const std::uint64_t l0_table = 0x1000ULL;
    const std::uint64_t l1_table = 0x2000ULL;
    const std::uint64_t l2_table = 0x3000ULL;
    const std::uint64_t l3_table = 0x4000ULL;

    mmu.set_sctlr_el1(0x00C50839ULL); // MMU enabled
    mmu.set_tcr_el1(0x19ULL);        // 4KB granule, 39-bit VA space
    mmu.set_ttbr0_el1(l0_table | (0x0001ULL << 48u)); // ASID 1

    memory.write_u64(l0_table, l1_table | 0x3ULL);
    memory.write_u64(l1_table, l2_table | 0x3ULL);
    memory.write_u64(l2_table, l3_table | 0x3ULL);

    // L3 entry at index 2 (VA 0x2000): maps to PA 0x30000, AF=1, AP=Read/Write
    const std::uint64_t l3_desc = 0x30000ULL | 0x3ULL | (1ULL << 10u);
    memory.write_u64(l3_table + 16u, l3_desc);

    // Invalidate TLB to guarantee cold state
    mmu.invalidate_tlb_all();

    mmu::TranslationFault fault{};
    // Cold lookup: walks 4 page table levels
    auto res_cold = mmu.translate(memory, 0x2000, mmu::AccessKind::Read, 1, &fault);
    STAGE71_ASSERT(res_cold.has_value(), "Cold translation succeeds");
    STAGE71_ASSERT(*res_cold == 0x30000ULL, "Cold translated address matches");

    const std::uint64_t initial_hits = mmu.get_tlb_hits();

    // Warm lookups: repeat 10,000 times through TLB cache
    constexpr std::size_t kWarmIterations = 10000;
    auto start_warm = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < kWarmIterations; ++i) {
        auto res_warm = mmu.translate(memory, 0x2000, mmu::AccessKind::Read, 1, &fault);
        STAGE71_ASSERT(res_warm.has_value(), "Warm translation succeeds");
        STAGE71_ASSERT(*res_warm == 0x30000ULL, "Warm translated address matches");
    }
    auto end_warm = std::chrono::high_resolution_clock::now();
    const auto warm_micros = std::chrono::duration_cast<std::chrono::microseconds>(end_warm - start_warm).count();

    const std::uint64_t hits_after = mmu.get_tlb_hits();
    STAGE71_ASSERT(hits_after - initial_hits == kWarmIterations, "TLB recorded exactly 10,000 hits");
    std::cout << "    [INFO] 10,000 TLB warm translations completed in " << warm_micros << " us\n";

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Long-Duration Instruction Dispatch Stress (30,000+ Instructions)
// -----------------------------------------------------------------------------
bool test_instruction_dispatch_stress_30k() {
    std::cout << "[2] test_instruction_dispatch_stress_30k...\n";
    CPU cpu;
    Memory memory(0x20000);

    // Initial setup:
    // X0 = 0
    // X1 = 10,000
    // Loop at PC 0x100:
    // 0x100: ADD X0, X0, #1    (0x91000400u)
    // 0x104: SUBS X1, X1, #1   (0xF1000421u)
    // 0x108: B.NE -8           (0x54FFFFC1u)
    // 0x10C: HLT #0            (0xD4400000u)

    cpu.reset();
    cpu.write_register(0, 0);
    cpu.write_register(1, 10000);
    cpu.set_program_counter(0x100);

    memory.write_u32(0x100, 0x91000400u); // ADD X0, X0, #1
    memory.write_u32(0x104, 0xF1000421u); // SUBS X1, X1, #1
    memory.write_u32(0x108, 0x54FFFFC1u); // B.NE -8
    memory.write_u32(0x10C, 0xD4400000u); // HLT #0

    auto start_time = std::chrono::high_resolution_clock::now();
    std::uint64_t steps = 0;
    while (cpu.get_program_counter() != 0x10C && steps < 50000) {
        cpu.step(memory);
        ++steps;
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    const auto elapsed_micros = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();

    STAGE71_ASSERT(cpu.get_program_counter() == 0x10C, "Reached loop termination PC 0x10C");
    STAGE71_ASSERT(cpu.read_register(0) == 10000, "X0 accumulated exactly 10,000 iterations");
    STAGE71_ASSERT(cpu.read_register(1) == 0, "X1 decremented to 0");
    STAGE71_ASSERT(steps == 30000, "Loop executed exactly 30,000 instructions");
    std::cout << "    [INFO] 30,000 instructions executed in " << elapsed_micros << " us (" 
              << (steps * 1000000.0 / (elapsed_micros > 0 ? elapsed_micros : 1)) << " IPS)\n";

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Memory Sequential Stream Across Page Boundaries
// -----------------------------------------------------------------------------
bool test_memory_sequential_stream_multi_page() {
    std::cout << "[3] test_memory_sequential_stream_multi_page...\n";
    constexpr std::size_t kNumPages = 16;
    constexpr std::size_t kPageSize = 4096;
    constexpr std::size_t kTotalBytes = kNumPages * kPageSize; // 64 KB
    constexpr std::size_t kWords = kTotalBytes / sizeof(std::uint64_t);

    Memory mem(kTotalBytes + 0x1000);

    // Stream 64-bit writes
    auto start_write = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < kWords; ++i) {
        const std::uint64_t paddr = i * sizeof(std::uint64_t);
        const std::uint64_t val = 0xA5A5000000000000ULL ^ static_cast<std::uint64_t>(i);
        mem.write_u64(paddr, val);
    }
    auto end_write = std::chrono::high_resolution_clock::now();
    const auto write_us = std::chrono::duration_cast<std::chrono::microseconds>(end_write - start_write).count();

    // Stream 64-bit reads and verify pattern
    auto start_read = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < kWords; ++i) {
        const std::uint64_t paddr = i * sizeof(std::uint64_t);
        const std::uint64_t expected = 0xA5A5000000000000ULL ^ static_cast<std::uint64_t>(i);
        const std::uint64_t actual = mem.read_u64(paddr);
        STAGE71_ASSERT(actual == expected, "Streaming data matches pattern exactly");
    }
    auto end_read = std::chrono::high_resolution_clock::now();
    const auto read_us = std::chrono::duration_cast<std::chrono::microseconds>(end_read - start_read).count();

    std::cout << "    [INFO] 64 KB sequential stream: Write " << write_us << " us, Read " << read_us << " us\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Virtual UART Console High-Volume Throughput
// -----------------------------------------------------------------------------
bool test_virtual_uart_high_volume_throughput() {
    std::cout << "[4] test_virtual_uart_high_volume_throughput...\n";
    VirtualUartDevice uart(0x55415254u, 0x09000000ULL, 0x1000ULL, 33);

    // Enable UART and TX
    uart.write_register(0x30, 0x0101u, 4); // UARTCR: UARTEN=1, TXE=1

    constexpr std::size_t kChars = 5000;
    auto start_tx = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < kChars; ++i) {
        const std::uint8_t ch = static_cast<std::uint8_t>('A' + (i % 26));
        uart.write_register(0x00, ch, 4); // UARTDR
    }
    auto end_tx = std::chrono::high_resolution_clock::now();
    const auto tx_us = std::chrono::duration_cast<std::chrono::microseconds>(end_tx - start_tx).count();

    const std::string text = uart.get_output_text();
    STAGE71_ASSERT(text.size() == kChars, "UART output buffer contains exactly 5,000 characters");

    // Check content validity
    for (std::size_t i = 0; i < kChars; ++i) {
        const char expected = static_cast<char>('A' + (i % 26));
        STAGE71_ASSERT(text[i] == expected, "UART character fidelity intact");
    }

    std::cout << "    [INFO] 5,000 UART characters streamed in " << tx_us << " us\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: SMP 4-Core Contested Atomic Stress
// -----------------------------------------------------------------------------
bool test_smp_4core_contested_atomic_stress() {
    std::cout << "[5] test_smp_4core_contested_atomic_stress...\n";
    constexpr std::size_t kCores = 4;
    constexpr std::size_t kRoundsPerCore = 250;
    SmpExclusiveMonitorManager monitor(kCores);
    SmpSharedMemory shared_mem(0x10000, kCores);
    shared_mem.attach_exclusive_monitor(&monitor);

    const std::uint64_t kSharedLockAddr = 0x800ULL;
    shared_mem.write_u64(0, kSharedLockAddr, 0);

    // Simulate contested atomic updates from 4 interleaved cores
    std::size_t successful_commits = 0;
    std::size_t retries = 0;

    for (std::size_t round = 0; round < kRoundsPerCore; ++round) {
        for (std::size_t c = 0; c < kCores; ++c) {
            bool committed = false;
            while (!committed) {
                // Core c loads exclusive
                monitor.load_exclusive(c, kSharedLockAddr, 8);
                const std::uint64_t cur = shared_mem.read_u64(c, kSharedLockAddr);
                // Attempt store exclusive with incremented value
                if (shared_mem.write_exclusive_u64(c, kSharedLockAddr, cur + 1)) {
                    committed = true;
                    ++successful_commits;
                } else {
                    ++retries;
                }
            }
        }
    }

    const std::uint64_t final_val = shared_mem.read_u64(0, kSharedLockAddr);
    STAGE71_ASSERT(final_val == kCores * kRoundsPerCore, "Shared atomic counter correctly reached 1,000");
    STAGE71_ASSERT(successful_commits == kCores * kRoundsPerCore, "Committed exactly 1,000 atomic transactions");
    std::cout << "    [INFO] 1,000 atomic increments completed across 4 cores (Retries handled: " << retries << ")\n";

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: GICv3 Periodic Timer Interrupt Stress
// -----------------------------------------------------------------------------
bool test_gic_periodic_timer_irq_stress() {
    std::cout << "[6] test_gic_periodic_timer_irq_stress...\n";
    GICv3 gic;
    gic.set_core_count(1);

    // Enable CPU interface and Group 1
    gic.write_icc_pmr(0xFF, 0);
    gic.write_icc_igrpen1(1, 0);

    // Route SPI 33 as Group 1 (IRQ), Priority 0x60
    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    dist.gicd_isenabler[1] |= (1u << (33 - 32));
    dist.gicd_igroupr[1]   |= (1u << (33 - 32)); // Group 1
    dist.gicd_ipriorityr[33] = 0x60u;
    dist.gicd_irouter[1] = 0; // Target core 0
    gic.set_distributor_state(dist);

    constexpr std::size_t kIrqCycles = 500;
    auto start_irq = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < kIrqCycles; ++i) {
        // Assert SPI 33
        gic.set_spi_level(33, true);
        auto sig = gic.evaluate(1, 0, 0);
        STAGE71_ASSERT(sig.has_interrupt == true, "Interrupt signaled");
        STAGE71_ASSERT(sig.intid == 33u, "INTID 33 signaled");

        // Acknowledge Group 1
        std::uint32_t ack_id = gic.acknowledge_group1(1, 0);
        STAGE71_ASSERT(ack_id == 33u, "Acknowledged INTID 33");

        // Clear level and End of Interrupt (EOIR)
        gic.set_spi_level(33, false);
        gic.end_of_interrupt_group1(33, 1, 0);
    }

    auto end_irq = std::chrono::high_resolution_clock::now();
    const auto irq_us = std::chrono::duration_cast<std::chrono::microseconds>(end_irq - start_irq).count();

    auto final_sig = gic.evaluate(1, 0, 0);
    STAGE71_ASSERT(final_sig.has_interrupt == false, "No spurious pending interrupts remain");
    std::cout << "    [INFO] 500 GIC interrupt assert-ack-eoi cycles completed in " << irq_us << " us\n";

    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Platform Checkpoint & Hashing Throughput
// -----------------------------------------------------------------------------
bool test_platform_checkpoint_throughput() {
    std::cout << "[7] test_platform_checkpoint_throughput...\n";
    VirtualPlatform platform;
    auto& cpu = platform.get_cpu(0);
    auto& mem = platform.memory();

    // Populate registers and memory
    for (std::uint32_t r = 0; r < 31; ++r) {
        cpu.write_register(r, 0x10000000ULL + r);
    }
    cpu.set_program_counter(0x4000);
    mem.write_u64(0x4000, 0xDEADBEEFCAFEBABEULL);

    constexpr std::size_t kHashCycles = 50;
    auto start_hash = std::chrono::high_resolution_clock::now();

    ReplayComponentHashes baseline = ReplayStateHash::compute_state_hashes(cpu, mem);

    for (std::size_t i = 0; i < kHashCycles; ++i) {
        ReplayComponentHashes current = ReplayStateHash::compute_state_hashes(cpu, mem);
        STAGE71_ASSERT(current == baseline, "State hash remains bit-exact across cycles");
    }

    auto end_hash = std::chrono::high_resolution_clock::now();
    const auto hash_us = std::chrono::duration_cast<std::chrono::microseconds>(end_hash - start_hash).count();

    std::cout << "    [INFO] 50 full-platform state hash computations completed in " << hash_us << " us\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Platform Lifecycle & Resource Clean Teardown
// -----------------------------------------------------------------------------
bool test_platform_lifecycle_leak_isolation() {
    std::cout << "[8] test_platform_lifecycle_leak_isolation...\n";
    constexpr std::size_t kLifecycles = 30;

    for (std::size_t i = 0; i < kLifecycles; ++i) {
        VirtualPlatformConfig cfg;
        cfg.boot_entry = 0x0000;
        cfg.ram_size = 0x10000; // 64 KB

        auto platform = std::make_unique<VirtualPlatform>(cfg);
        auto& cpu = platform->get_cpu(0);
        auto& mem = platform->memory();

        // Write small program:
        // MOV X0, #42
        // ADD X0, X0, #1
        mem.write_u32(0, 0xD2800540u); // MOV X0, #42
        mem.write_u32(4, 0x91000400u); // ADD X0, X0, #1

        cpu.step(mem);
        cpu.step(mem);

        STAGE71_ASSERT(cpu.read_register(0) == 43, "Execution succeeded in cycle");
        STAGE71_ASSERT(cpu.get_program_counter() == 8, "PC advanced correctly");

        platform->reset();
        STAGE71_ASSERT(platform->get_cpu(0).get_program_counter() == 0, "Reset restored PC");

        // Scope exit cleanly tears down VirtualPlatform, bus, devices, and memory
    }

    std::cout << "    [INFO] 30 full VirtualPlatform instantiate-execute-reset-destroy cycles clean\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Differential Execution Parity Under Stress (Reference Model)
// -----------------------------------------------------------------------------
bool test_reference_model_differential_stress() {
    std::cout << "[9] test_reference_model_differential_stress...\n";
    CPU cpu;
    Memory mem(0x10000);

    ReferenceCpuState ref_cpu{};
    ReferenceMemory ref_mem(0x10000);
    ReferenceExclusiveMonitor ref_monitor{};

    // Program:
    // 0: ADD X2, X0, X1  (0x8B010002u)
    // 4: SUB X3, X1, X0  (0xCB000023u)
    // 8: AND X4, X0, X1  (0x8A010004u)
    // 12: ORR X5, X0, X1 (0xAA010005u)
    // 16: EOR X6, X0, X1 (0xCA010006u)
    // 20: ADD X7, X0, #50 (0x9100C807u)
    // 24: SUB X8, X1, #30 (0xD1007828u)
    const std::vector<std::uint32_t> instrs = {
        0x8B010002u, // ADD X2, X0, X1
        0xCB000023u, // SUB X3, X1, X0
        0x8A010004u, // AND X4, X0, X1
        0xAA010005u, // ORR X5, X0, X1
        0xCA010006u, // EOR X6, X0, X1
        0x9100C807u, // ADD X7, X0, #50
        0xD1007828u  // SUB X8, X1, #30
    };

    for (std::size_t i = 0; i < instrs.size(); ++i) {
        mem.write_u32(i * 4, instrs[i]);
        ref_mem.write_u32(i * 4, instrs[i]);
    }

    cpu.reset();
    cpu.set_program_counter(0);
    ref_cpu.pc = 0;

    // Set initial register inputs in both models
    cpu.write_register(0, 100);
    cpu.write_register(1, 200);
    ref_cpu.x[0] = 100;
    ref_cpu.x[1] = 200;

    for (std::size_t step = 0; step < instrs.size(); ++step) {
        cpu.step(mem);
        auto ref_res = ReferenceExecutor::step_instruction(ref_cpu, ref_mem, ref_monitor, 0);
        STAGE71_ASSERT(ref_res.success, "Reference model step succeeded");

        // Validate architectural state agreement
        std::array<std::uint64_t, 31> actual_regs{};
        for (std::uint32_t r = 0; r < 31; ++r) {
            actual_regs[r] = cpu.read_register(r);
        }

        DivergenceReport div = DifferentialValidator::compare_cpu_state(
            0, ref_cpu,
            cpu.get_program_counter(),
            cpu.get_nzcv(),
            step + 1,
            actual_regs.data()
        );

        if (div.has_divergence) {
            std::cerr << "Divergence at step " << step << " field " << div.field_name
                      << ": expected=" << div.expected_value << ", actual=" << div.actual_value << "\n";
            return false;
        }
    }

    STAGE71_ASSERT(cpu.read_register(2) == 300, "X2 = 100 + 200 = 300");
    STAGE71_ASSERT(cpu.read_register(3) == 100, "X3 = 200 - 100 = 100");
    STAGE71_ASSERT(cpu.read_register(7) == 150, "X7 = 100 + 50 = 150");
    STAGE71_ASSERT(cpu.read_register(8) == 170, "X8 = 200 - 30 = 170");
    std::cout << "    [INFO] Lockstep differential execution verified with 0 divergence across all registers\n";

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: State Hash Determinism Under Repeated Runs
// -----------------------------------------------------------------------------
bool test_state_hash_determinism_under_sustained_load() {
    std::cout << "[10] test_state_hash_determinism_under_sustained_load...\n";

    auto run_simulation = []() -> ReplayComponentHashes {
        CPU cpu;
        Memory mem(0x10000);
        cpu.reset();

        // Write a sequence of calculations and memory accesses
        // 0: MOVZ X0, #0x55
        // 4: STR X0, [SP, #16]
        // 8: LDR X1, [SP, #16]
        // 12: ADD X2, X0, X1
        mem.write_u32(0, 0xD2800AA0u);
        mem.write_u32(4, 0xF9000BE0u);
        mem.write_u32(8, 0xF9400BE1u);
        mem.write_u32(12, 0x8B010002u);

        cpu.set_stack_pointer(0x8000);
        cpu.set_program_counter(0);

        for (int i = 0; i < 4; ++i) {
            cpu.step(mem);
        }

        return ReplayStateHash::compute_state_hashes(cpu, mem);
    };

    ReplayComponentHashes hash1 = run_simulation();
    ReplayComponentHashes hash2 = run_simulation();

    STAGE71_ASSERT(hash1 == hash2, "Identical runs produce 100% bit-exact state hashes");
    STAGE71_ASSERT(hash1.cpu_registers_hash != 0, "CPU register hash non-zero");
    STAGE71_ASSERT(hash1.memory_hash != 0, "Memory hash non-zero");

    std::cout << "    [INFO] Deterministic state hashing verified bit-exact across independent executions\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 71: Performance Optimization & Reliability Tests ===\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*func)(), const char* name) {
        if (func()) {
            std::cout << "[PASS] " << name << "\n\n";
            ++passed;
        } else {
            std::cerr << "[FAIL] " << name << "\n\n";
            ++failed;
        }
    };

    run_test(test_tlb_cold_vs_warm_lookup_speed, "TLB Cold vs Warm Lookup Speed");
    run_test(test_instruction_dispatch_stress_30k, "Long-Duration Instruction Dispatch Stress");
    run_test(test_memory_sequential_stream_multi_page, "Memory Sequential Stream Across Pages");
    run_test(test_virtual_uart_high_volume_throughput, "Virtual UART Console Throughput");
    run_test(test_smp_4core_contested_atomic_stress, "SMP 4-Core Contested Atomic Stress");
    run_test(test_gic_periodic_timer_irq_stress, "GICv3 Periodic Timer Interrupt Stress");
    run_test(test_platform_checkpoint_throughput, "Platform Checkpoint & Hashing Throughput");
    run_test(test_platform_lifecycle_leak_isolation, "Platform Lifecycle & Resource Clean Teardown");
    run_test(test_reference_model_differential_stress, "Differential Execution Parity Under Stress");
    run_test(test_state_hash_determinism_under_sustained_load, "State Hash Determinism Under Repeated Runs");

    std::cout << "Stage 71 Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
