#include <iostream>
#include <vector>
#include <cstdint>
#include <cstddef>
#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/smp_cluster.h"
#include "kernel/cpu_context.h"
#include "kernel/exception_frame_manager.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::kernel;

// -----------------------------------------------------------------------------
// Compile-Time Assertions for All 20 Field Offsets in 896-byte ExceptionFrame
// (Acceptance Test I)
// -----------------------------------------------------------------------------
static_assert(sizeof(ExceptionFrame) == 896, "ExceptionFrame must be exactly 896 bytes");
static_assert(offsetof(ExceptionFrame, magic) == 0x000, "magic offset must be 0x000");
static_assert(offsetof(ExceptionFrame, frame_size) == 0x008, "frame_size offset must be 0x008");
static_assert(offsetof(ExceptionFrame, elr_el1) == 0x010, "elr_el1 offset must be 0x010");
static_assert(offsetof(ExceptionFrame, spsr_el1) == 0x018, "spsr_el1 offset must be 0x018");
static_assert(offsetof(ExceptionFrame, esr_el1) == 0x020, "esr_el1 offset must be 0x020");
static_assert(offsetof(ExceptionFrame, far_el1) == 0x028, "far_el1 offset must be 0x028");
static_assert(offsetof(ExceptionFrame, sp_el0) == 0x030, "sp_el0 offset must be 0x030");
static_assert(offsetof(ExceptionFrame, sp_el1) == 0x038, "sp_el1 offset must be 0x038");
static_assert(offsetof(ExceptionFrame, tpidr_el0) == 0x040, "tpidr_el0 offset must be 0x040");
static_assert(offsetof(ExceptionFrame, tpidr_el1) == 0x048, "tpidr_el1 offset must be 0x048");
static_assert(offsetof(ExceptionFrame, asid) == 0x050, "asid offset must be 0x050");
static_assert(offsetof(ExceptionFrame, ttbr0_el1) == 0x058, "ttbr0_el1 offset must be 0x058");
static_assert(offsetof(ExceptionFrame, fpcr) == 0x060, "fpcr offset must be 0x060");
static_assert(offsetof(ExceptionFrame, fpsr) == 0x064, "fpsr offset must be 0x064");
static_assert(offsetof(ExceptionFrame, exception_origin_el) == 0x068, "exception_origin_el offset must be 0x068");
static_assert(offsetof(ExceptionFrame, simd_saved) == 0x06C, "simd_saved offset must be 0x06C");
static_assert(offsetof(ExceptionFrame, gpr) == 0x070, "gpr offset must be 0x070");
static_assert(offsetof(ExceptionFrame, pad0) == 0x168, "pad0 offset must be 0x168");
static_assert(offsetof(ExceptionFrame, q_regs) == 0x170, "q_regs offset must be 0x170");
static_assert(offsetof(ExceptionFrame, reserved) == 0x370, "reserved offset must be 0x370");

// -----------------------------------------------------------------------------
// Test I1: Full CPU Context Preservation (Acceptance Test I)
// -----------------------------------------------------------------------------
static bool test_cpu_context_preservation() {
    CPU cpu1;
    cpu1.set_current_el(1);

    // Initialize all 31 general purpose registers with unique values
    for (std::size_t i = 0; i < 31; ++i) {
        cpu1.write_register(i, 0x1000000000000000ULL | (static_cast<std::uint64_t>(i) << 32) | (i * 0x1111));
    }
    cpu1.set_program_counter(0x80001000ULL);
    cpu1.set_sp_el0(0x7FFFFFFF0000ULL);
    cpu1.set_sp_el1(0xFFFFFF800000ULL);
    cpu1.set_elr_el1(0x80001000ULL);
    cpu1.set_daif(0x2); // IRQ masked
    cpu1.set_nzcv(0x8); // N flag set
    cpu1.set_spsr_el1(static_cast<std::uint32_t>((0x8ULL << 28) | (0x2ULL << 6) | 0x00000005ULL));
    cpu1.set_tpidr_el0(0x123456789ABCDEF0ULL);
    cpu1.set_tpidr_el1(0x0FEDCBA987654321ULL);
    cpu1.set_cpacr_el1(0x00300000ULL);
    cpu1.set_fpcr(0x01000000);
    cpu1.set_fpsr(0x00000008);

    // Initialize all 32 vector registers Q0..Q31
    for (std::size_t i = 0; i < 32; ++i) {
        cpu1.write_vector_register(i, CPU::VectorRegister{0xAAA00000ULL | i, 0xBBB00000ULL | i});
    }

    // Capture context
    ArchitecturalCpuContext ctx1;
    ctx1.capture(cpu1);

    // Serialize to guest memory
    Memory mem(0x10000);
    constexpr std::uint64_t kFrameAddr = 0x2000;
    TEST_ASSERT(ctx1.write_frame_to_memory(mem, kFrameAddr), "Frame written to memory");

    // Deserialize into context 2
    ArchitecturalCpuContext ctx2;
    TEST_ASSERT(ctx2.read_frame_from_memory(mem, kFrameAddr), "Frame read from memory");

    // Verify context equality
    TEST_ASSERT(ctx1.equals(ctx2), "Deserialized context is bit-exact identical to captured context");

    // Restore into a fresh CPU2
    CPU cpu2;
    ctx2.restore(cpu2);

    // Assert every register matches exactly
    for (std::size_t i = 0; i < 31; ++i) {
        TEST_ASSERT(cpu2.read_register(i) == cpu1.read_register(i), "GPR match");
    }
    TEST_ASSERT(cpu2.get_program_counter() == cpu1.get_program_counter(), "PC match");
    TEST_ASSERT(cpu2.get_sp_el0() == cpu1.get_sp_el0(), "SP_EL0 match");
    TEST_ASSERT(cpu2.get_sp_el1() == cpu1.get_sp_el1(), "SP_EL1 match");
    TEST_ASSERT(cpu2.get_elr_el1() == cpu1.get_elr_el1(), "ELR_EL1 match");
    TEST_ASSERT(cpu2.get_spsr_el1() == cpu1.get_spsr_el1(), "SPSR_EL1 match");
    TEST_ASSERT(cpu2.get_daif() == cpu1.get_daif(), "DAIF match");
    TEST_ASSERT(cpu2.get_nzcv() == cpu1.get_nzcv(), "NZCV match");
    TEST_ASSERT(cpu2.get_tpidr_el0() == cpu1.get_tpidr_el0(), "TPIDR_EL0 match");
    TEST_ASSERT(cpu2.get_tpidr_el1() == cpu1.get_tpidr_el1(), "TPIDR_EL1 match");
    TEST_ASSERT(cpu2.get_fpcr() == cpu1.get_fpcr(), "FPCR match");
    TEST_ASSERT(cpu2.get_fpsr() == cpu1.get_fpsr(), "FPSR match");

    for (std::size_t i = 0; i < 32; ++i) {
        auto v1 = cpu1.read_vector_register(i);
        auto v2 = cpu2.read_vector_register(i);
        TEST_ASSERT(v1.low == v2.low && v1.high == v2.high, "Vector reg match");
    }
    return true;
}

// -----------------------------------------------------------------------------
// Test J1: Cross-Core TLB Invalidation (Acceptance Test J)
// -----------------------------------------------------------------------------
static bool test_cross_core_tlb_invalidation() {
    SMPCluster cluster(2, 0x400000ULL);
    auto& mem = cluster.get_memory();

    auto& core0 = cluster.get_core(0);
    auto& core1 = cluster.get_core(1);

    auto& mmu0 = core0.get_mmu();
    auto& mmu1 = core1.get_mmu();

    // Configure 16KB Stage 1 MMU translation for both cores
    constexpr std::uint64_t l2_pa = 0x40000ULL;
    constexpr std::uint64_t l3_pa = 0x48000ULL;
    constexpr std::uint64_t initial_pa = 0x50000ULL;
    constexpr std::uint16_t asid = 1;

    mmu0.set_sctlr_el1(0x00C50839ULL);
    mmu0.set_tcr_el1((2ULL << 14u) | 28ULL);
    mmu0.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l2_pa);

    mmu1.set_sctlr_el1(0x00C50839ULL);
    mmu1.set_tcr_el1((2ULL << 14u) | 28ULL);
    mmu1.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l2_pa);

    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);
    // L3 page descriptor for VA 0x0000 -> initial_pa (0x50000)
    const std::uint64_t desc = initial_pa | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), desc);

    // Warm Core 1's TLB
    mmu::TranslationFault fault{};
    auto res1 = mmu1.translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(res1.has_value() && *res1 == initial_pa + 0x1000ULL, "Core 1 initial translation resolved");
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 1, "Core 1 TLB cached 1 translation");

    // Core 0 broadcasts TLBI VA for 0x1000 on ASID 1
    cluster.broadcast_tlbi_va(0, 0x1000ULL, asid, false);

    // Core 1's TLB MUST now be invalidated (Acceptance Test J)
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 0, "Core 1 TLB invalidated by Core 0 broadcast");
    TEST_ASSERT(mmu1.get_tlb_invalidations() >= 1, "Core 1 invalidation counter incremented");

    // Re-warm Core 1 TLB
    auto res2 = mmu1.translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(res2.has_value(), "Core 1 translation succeeded");
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 1, "Core 1 TLB re-cached entry");

    // Core 0 broadcasts TLBI ASID
    cluster.broadcast_tlbi_asid(0, asid);
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 0, "Core 1 TLB invalidated by TLBI ASID broadcast");

    // Re-warm and broadcast TLBI ALL
    mmu1.translate(mem, 0x1000ULL, mmu::AccessKind::Read, 1, &fault);
    cluster.broadcast_tlbi_all(0);
    TEST_ASSERT(mmu1.get_tlb_entry_count() == 0, "Core 1 TLB invalidated by TLBI ALL broadcast");
    return true;
}

int main() {
    std::cout << "[STAGE 196.11 & 196.12] Running TLB Invalidation & Context Invariants (Acceptance Tests I & J)..." << std::endl;

    if (!test_cpu_context_preservation()) {
        std::cerr << "test_cpu_context_preservation FAILED" << std::endl;
        return 1;
    }
    if (!test_cross_core_tlb_invalidation()) {
        std::cerr << "test_cross_core_tlb_invalidation FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 196.11 & 196.12] All TLB and Context Tests PASSED (Acceptance Tests I & J VERIFIED)." << std::endl;
    return 0;
}
