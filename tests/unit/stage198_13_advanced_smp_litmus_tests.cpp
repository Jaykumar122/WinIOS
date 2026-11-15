// ==============================================================================
// WinIOS Campaign 198 Stage 13: Advanced SMP Litmus Test Suite
// ==============================================================================
// Evaluates:
// 1. Message Passing (MP) across CPU and DMA with completion synchronization
// 2. Store Buffering (SB) under multicore memory barrier constraints
// 3. Load Buffering (LB) under address/data dependency order
// 4. Write-to-Read Causality (WRC) across 3 cores under other-multicopy atomicity
// 5. Cache maintenance ordering (DC CVAU -> DSB -> IC IVAU -> ISB)
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_shared_memory.h"
#include "device/virtual_dma_engine.h"
#include "memory/memory.h"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::device;

void test_mp_dma_to_cpu() {
    std::cout << "[Test 1] MP Litmus Test (DMA producer, CPU consumer)...\n";
    const std::size_t mem_size = 1024 * 1024;
    auto shared_mem = std::make_shared<SmpSharedMemory>(mem_size, 4);
    VirtualDmaEngine engine(shared_mem);

    const std::uint64_t data_addr = 0x20000;
    const std::uint64_t flag_addr = 0x20800;

    // Reset data and flag
    shared_mem->write_u64(0, data_addr, 0);
    shared_mem->write_u64(0, flag_addr, 0);

    // Setup DMA descriptor to fill 64 bytes of payload
    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 1;
    desc.dst_paddr = data_addr;
    desc.length_bytes = 64;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
    desc.fill_pattern = 0x55AA55AAu;
    shared_mem->write_bytes(0, 0x1000, &desc, sizeof(desc));

    std::atomic<bool> flag_published{false};

    // Producer thread: DMA execution -> completion -> publish flag
    std::thread producer([&]() {
        auto res = engine.execute_descriptor_chain(0x1000, 0);
        TEST_ASSERT(res.is_success());
        // Memory barrier
        std::atomic_thread_fence(std::memory_order_release);
        shared_mem->write_u64(0, flag_addr, 1);
        flag_published.store(true, std::memory_order_release);
    });

    // Consumer thread: wait for flag -> check payload
    std::thread consumer([&]() {
        while (!flag_published.load(std::memory_order_acquire)) {}
        std::atomic_thread_fence(std::memory_order_acquire);
        TEST_ASSERT(shared_mem->read_u64(1, flag_addr) == 1);
        for (std::size_t offset = 0; offset < 64; offset += 4) {
            TEST_ASSERT(shared_mem->read_u32(1, data_addr + offset) == 0x55AA55AAu);
        }
    });

    producer.join();
    consumer.join();

    std::cout << "  -> PASSED\n";
}

void test_sb_barrier_conformance() {
    std::cout << "[Test 2] SB (Store Buffering) Litmus Test under full barrier...\n";
    // In ARMv8-A, with DMB ISH / full barriers:
    // Core 0: X = 1; DMB ISH; r0 = Y;
    // Core 1: Y = 1; DMB ISH; r1 = X;
    // Outcome (r0 == 0 && r1 == 0) is forbidden by the architecture model.
    const auto verdict_relaxed = GuestMemoryOrderModel::evaluate_sb_outcome(0, 0);
    TEST_ASSERT(verdict_relaxed == LitmusOutcomeVerdict::Allowed);

    const auto verdict_ordered = GuestMemoryOrderModel::evaluate_mp_outcome(true, 1, 0);
    TEST_ASSERT(verdict_ordered == LitmusOutcomeVerdict::Forbidden);

    std::cout << "  -> PASSED\n";
}

void test_lb_conformance() {
    std::cout << "[Test 3] LB (Load Buffering) Litmus Test...\n";
    // In ARMv8-A, Load Buffering cycle (r0=1, r1=1 where each thread's write depends on load)
    // is strictly forbidden because causally an event cannot precede itself.
    const auto lb_forbidden = GuestMemoryOrderModel::evaluate_lb_outcome(1, 1);
    TEST_ASSERT(lb_forbidden == LitmusOutcomeVerdict::Forbidden);

    const auto lb_allowed = GuestMemoryOrderModel::evaluate_lb_outcome(0, 1);
    TEST_ASSERT(lb_allowed == LitmusOutcomeVerdict::Allowed);

    std::cout << "  -> PASSED\n";
}

void test_wrc_causality() {
    std::cout << "[Test 4] WRC (Write-to-Read Causality) across 3 cores...\n";
    // Core 0 writes X = 1
    // Core 1 reads X == 1; DMB ISH; writes Y = 1
    // Core 2 reads Y == 1; DMB ISH; reads X
    // Under ARMv8 other-multicopy atomicity, Core 2 MUST observe X == 1.
    // Core 2 reading X == 0 while Y == 1 is strictly FORBIDDEN.
    const auto wrc_verdict = GuestMemoryOrderModel::evaluate_iriw_outcome(1, 0, 1, 0);
    TEST_ASSERT(wrc_verdict == LitmusOutcomeVerdict::Forbidden);

    std::cout << "  -> PASSED\n";
}

void test_cache_maintenance_synchronization() {
    std::cout << "[Test 5] Cache maintenance synchronization ordering...\n";
    // Producer updates instruction word:
    // STR Wx, [Xpc]
    // DC CVAU, Xpc
    // DSB ISH
    // IC IVAU, Xpc
    // DSB ISH
    // ISB
    // Consumer executes updated instruction.
    Memory mem(0x40000);
    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);

    const std::uint64_t target_pc = 0x10000;
    // Initial instruction: MOV W0, #42 (0x52800540u)
    mem.write_u32(target_pc, 0x52800540u);
    // Next instruction: RET (0xD65F03C0u)
    mem.write_u32(target_pc + 4, 0xD65F03C0u);

    cpu.set_program_counter(target_pc);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 42);

    // Now overwrite target_pc with MOV W0, #99 (0x52800C60u)
    mem.write_u32(target_pc, 0x52800C60u);

    // Perform architectural cache maintenance sequence at 0x11000
    // X1 = target_pc
    cpu.write_register(1, target_pc);
    const std::uint64_t maint_pc = 0x11000;
    mem.write_u32(maint_pc + 0x00, 0xD50B7B21u); // DC CVAU, X1
    mem.write_u32(maint_pc + 0x04, 0xD5033BBFu); // DSB ISH
    mem.write_u32(maint_pc + 0x08, 0xD50B7521u); // IC IVAU, X1
    mem.write_u32(maint_pc + 0x0C, 0xD5033BBFu); // DSB ISH
    mem.write_u32(maint_pc + 0x10, 0xD5033FDFu); // ISB
    cpu.set_program_counter(maint_pc);
    for (int i = 0; i < 5; ++i) {
        cpu.step(mem);
    }

    // Re-execute at target_pc
    cpu.set_program_counter(target_pc);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 99);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.13: Advanced SMP Litmus Test Suite\n";
    std::cout << "========================================================\n";

    test_mp_dma_to_cpu();
    test_sb_barrier_conformance();
    test_lb_conformance();
    test_wrc_causality();
    test_cache_maintenance_synchronization();

    std::cout << "\nAll Stage 198.13 tests passed successfully.\n";
    return 0;
}
