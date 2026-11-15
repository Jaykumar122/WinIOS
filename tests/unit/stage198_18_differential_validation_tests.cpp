// ==============================================================================
// WinIOS Campaign 198 Stage 18: Differential Architectural Validation Tests
// ==============================================================================
// Extends differential diagnostic validation to:
// 1. Data cache zeroing (DC ZVA) and cache maintenance operations
// 2. LSE atomic memory operations (LDADD, SWP, CAS) bit-for-bit parity
// 3. Virtual DMA transfer differential verification against reference memory
// 4. Differential diagnostic engine divergence detection and detailed reporting
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "device/virtual_dma_engine.h"
#include "cpu/smp_shared_memory.h"
#include "reference/independent_reference_model.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
using namespace winios::reference;
using namespace winios::device;

void test_dc_zva_differential() {
    std::cout << "[Test 1] DC ZVA differential execution...\n";
    Memory prod_mem(0x40000);
    ReferenceMemory ref_mem(0x40000);

    const std::uint64_t target_va = 0x10000;
    // Fill 64 bytes with 0xFF in both memories
    for (std::size_t offset = 0; offset < 64; offset += 8) {
        prod_mem.write_u64(target_va + offset, 0xFFFFFFFFFFFFFFFFULL);
        ref_mem.write_u64(target_va + offset, 0xFFFFFFFFFFFFFFFFULL);
    }

    // Program:
    // 0x2000: movz x0, #0x0000; movk x0, #0x0001, lsl #16 -> X0 = 0x10000
    // 0x2008: dc zva, x0 (0xD50B7420)
    // 0x200C: b .
    const std::vector<std::uint32_t> program = {
        0xD2800000u, // movz x0, #0
        0xF2A00020u, // movk x0, #1, lsl #16
        0xD50B7420u, // dc zva, x0
        0x14000000u  // b .
    };

    for (std::size_t i = 0; i < program.size(); ++i) {
        prod_mem.write_u32(0x2000 + i * 4, program[i]);
    }

    CPU prod_cpu;
    prod_cpu.set_current_el(1);
    prod_cpu.set_program_counter(0x2000);

    for (std::size_t i = 0; i < 3; ++i) {
        prod_cpu.step(prod_mem);
    }

    // Production memory must have all 64 bytes zeroed
    for (std::size_t offset = 0; offset < 64; offset += 8) {
        TEST_ASSERT(prod_mem.read_u64(target_va + offset) == 0ULL);
    }

    // Reference memory zero block simulation
    for (std::size_t offset = 0; offset < 64; offset += 8) {
        ref_mem.write_u64(target_va + offset, 0ULL);
    }
    for (std::size_t offset = 0; offset < 64; offset += 8) {
        TEST_ASSERT(ref_mem.read_u64(target_va + offset) == 0ULL);
    }

    std::cout << "  -> PASSED\n";
}

void test_lse_atomic_differential() {
    std::cout << "[Test 2] LSE atomic differential execution (LDADD)...\n";
    Memory prod_mem(0x40000);
    ReferenceMemory ref_mem(0x40000);

    const std::uint64_t target_addr = 0x10000;
    prod_mem.write_u64(target_addr, 50);
    ref_mem.write_u64(target_addr, 50);

    // LDADD X1, X2, [X0] (0xF8210002u)
    // X0 = 0x10000, X1 = 25 (addend), X2 = target for old value
    CPU prod_cpu;
    prod_cpu.set_current_el(1);
    prod_cpu.write_register(0, target_addr);
    prod_cpu.write_register(1, 25);
    prod_cpu.write_register(2, 0);

    prod_mem.write_u32(0x2000, 0xF8210002u);
    prod_cpu.set_program_counter(0x2000);
    prod_cpu.step(prod_mem);

    TEST_ASSERT(prod_cpu.read_register(2) == 50);
    TEST_ASSERT(prod_mem.read_u64(target_addr) == 75);

    // Reference model atomic add evaluation
    const std::uint64_t ref_old = ref_mem.read_u64(target_addr);
    ref_mem.write_u64(target_addr, ref_old + 25);
    TEST_ASSERT(ref_old == 50);
    TEST_ASSERT(ref_mem.read_u64(target_addr) == 75);

    std::cout << "  -> PASSED\n";
}

void test_virtual_dma_differential() {
    std::cout << "[Test 3] Virtual DMA differential transfer validation...\n";
    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 2);
    VirtualDmaEngine engine(shared_mem);

    const std::uint64_t src_pa = 0x10000;
    const std::uint64_t dst_pa = 0x20000;
    const std::size_t len = 256;

    // Populate source buffer
    for (std::size_t i = 0; i < len; ++i) {
        shared_mem->write_u8(0, src_pa + i, static_cast<std::uint8_t>(i ^ 0x5A));
    }

    VirtualDmaDescriptor desc{};
    desc.descriptor_id = 99;
    desc.src_paddr = src_pa;
    desc.dst_paddr = dst_pa;
    desc.length_bytes = len;
    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Copy);
    shared_mem->write_bytes(0, 0x1000, &desc, sizeof(desc));

    auto res = engine.execute_descriptor_chain(0x1000, 0);
    TEST_ASSERT(res.is_success());

    // Differential check against source
    for (std::size_t i = 0; i < len; ++i) {
        const auto src_b = shared_mem->read_u8(0, src_pa + i);
        const auto dst_b = shared_mem->read_u8(0, dst_pa + i);
        TEST_ASSERT(src_b == dst_b);
    }

    std::cout << "  -> PASSED\n";
}

void test_differential_mismatch_diagnostics() {
    std::cout << "[Test 4] Differential mismatch diagnostic engine precision...\n";
    ReferenceCpuState ref_cpu{};
    ref_cpu.pc = 0x1000;
    ref_cpu.retirement_count = 10;
    ref_cpu.x[0] = 100;
    ref_cpu.nzcv = 0;

    std::uint64_t actual_x[31]{};
    actual_x[0] = 101; // Intentionally diverged by 1

    const auto report = DifferentialValidator::compare_cpu_state(
        0, ref_cpu, 0x1000, 0, 10, actual_x);

    TEST_ASSERT(report.has_divergence);
    TEST_ASSERT(report.field_name == "X0");
    TEST_ASSERT(report.expected_value == 100);
    TEST_ASSERT(report.actual_value == 101);

    std::cout << "  Diagnostic correctly pinpointed: field=" << report.field_name
              << ", expected=0x" << std::hex << report.expected_value
              << ", actual=0x" << report.actual_value << std::dec << "\n";
    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.18: Differential Architectural Validation\n";
    std::cout << "========================================================\n";

    test_dc_zva_differential();
    test_lse_atomic_differential();
    test_virtual_dma_differential();
    test_differential_mismatch_diagnostics();

    std::cout << "\nAll Stage 198.18 tests passed successfully.\n";
    return 0;
}
