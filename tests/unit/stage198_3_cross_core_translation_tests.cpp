// =============================================================================
// WinIOS Campaign 198 Stage 198.3: Cross-Core Translation and Memory Tests
// =============================================================================

#include "cpu/cpu.h"
#include "cpu/smp_cluster.h"
#include "cpu/mmu.h"

#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace winios;

namespace {

// Configures MMU with 16KB granule and 36-bit VA space
void setup_mmu_tables(Memory& mem, mmu::MMU& mmu, std::uint64_t ttbr_base, std::uint16_t asid) {
    (void)mem;
    const std::uint64_t mair = 0x00FFULL; // Normal WB Attr0
    mmu.set_mair_el1(mair);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL); // TG0=16KB, T0SZ=28
    const std::uint64_t ttbr_val = (static_cast<std::uint64_t>(asid) << 48u) | ttbr_base;
    mmu.set_ttbr0_el1(ttbr_val);
    mmu.set_sctlr_el1(0x5ULL); // MMU enabled
}

void test_cross_core_tlbi_broadcast() {
    std::cout << "[Test 1] Cross-core TLBI broadcast invalidation in SMP cluster...\n";
    SMPCluster cluster(2, 1024 * 1024);
    Memory& mem = cluster.get_memory();

    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);

    const std::uint64_t ttbr_base = 0x10000;
    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);

    // Map VA 0x0000 -> PA 0x40000 (non-global, ASID 1)
    // Non-global bit is bit 11 (nG = 1)
    const std::uint64_t l3_desc = 0x40000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u);
    mem.write_u64(l3_base, l3_desc);

    setup_mmu_tables(mem, cpu0.get_mmu(), ttbr_base, 1);
    setup_mmu_tables(mem, cpu1.get_mmu(), ttbr_base, 1);

    mmu::TranslationFault fault;
    bool is_dev = false;

    // Both cores translate VA 0x0000 and populate their respective TLBs
    auto r0 = cpu0.get_mmu().translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r0.has_value() && r0.value() == 0x40000ULL);
    TEST_ASSERT(cpu0.get_mmu().get_tlb_entry_count() > 0);

    auto r1 = cpu1.get_mmu().translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r1.has_value() && r1.value() == 0x40000ULL);
    TEST_ASSERT(cpu1.get_mmu().get_tlb_entry_count() > 0);

    // Core 0 executes TLBI VAE1IS (va=0x0000, asid=1): local invalidation + broadcast
    cpu0.get_mmu().invalidate_tlb_va(0x0000, 1, false);
    cluster.broadcast_tlbi_va(0, 0x0000, 1, false);

    // Verify Core 1's TLB was invalidated
    TEST_ASSERT(cpu1.get_mmu().get_tlb_entry_count() == 0);
    // Core 0's own TLB was also invalidated
    TEST_ASSERT(cpu0.get_mmu().get_tlb_entry_count() == 0);

    std::cout << "  -> PASSED\n";
}

void test_asid_isolation() {
    std::cout << "[Test 2] ASID translation isolation...\n";
    Memory mem(1024 * 1024);
    mmu::MMU mmu_inst;

    // Setup Process 1 (ASID 1): VA 0x0000 -> PA 0x40000
    const std::uint64_t ttbr1_base = 0x10000;
    const std::uint64_t l3_proc1 = ttbr1_base + 0x4000;
    mem.write_u64(ttbr1_base, l3_proc1 | 0x3ULL);
    mem.write_u64(l3_proc1, 0x40000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u)); // nG=1

    // Setup Process 2 (ASID 2): VA 0x0000 -> PA 0x50000
    const std::uint64_t ttbr2_base = 0x20000;
    const std::uint64_t l3_proc2 = ttbr2_base + 0x4000;
    mem.write_u64(ttbr2_base, l3_proc2 | 0x3ULL);
    mem.write_u64(l3_proc2, 0x50000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u)); // nG=1

    // Run as Process 1 (ASID 1)
    setup_mmu_tables(mem, mmu_inst, ttbr1_base, 1);
    mmu::TranslationFault fault;
    auto res1 = mmu_inst.translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(res1.has_value() && res1.value() == 0x40000ULL);

    // Context switch to Process 2 (ASID 2) without flushing global TLB
    // TTBR0 base change triggers full flush or ASID tag check
    setup_mmu_tables(mem, mmu_inst, ttbr2_base, 2);
    auto res2 = mmu_inst.translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(res2.has_value() && res2.value() == 0x50000ULL);

    // Switch back to Process 1 (ASID 1)
    setup_mmu_tables(mem, mmu_inst, ttbr1_base, 1);
    auto res3 = mmu_inst.translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(res3.has_value() && res3.value() == 0x40000ULL);

    std::cout << "  -> PASSED\n";
}

void test_page_reuse_and_stale_tlb_invalidation() {
    std::cout << "[Test 3] Physical page reuse and stale TLB invalidation...\n";
    SMPCluster cluster(2, 1024 * 1024);
    Memory& mem = cluster.get_memory();

    CPU& cpu0 = cluster.get_core(0);
    CPU& cpu1 = cluster.get_core(1);

    const std::uint64_t ttbr_base = 0x10000;
    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);

    // Initial mapping: VA 0x0000 -> PA 0x40000 (contains secret "SECRET1")
    mem.write_u64(l3_base, 0x40000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));
    mem.write_u64(0x40000, 0x1111222233334444ULL);

    setup_mmu_tables(mem, cpu0.get_mmu(), ttbr_base, 1);
    setup_mmu_tables(mem, cpu1.get_mmu(), ttbr_base, 1);

    // Core 1 reads VA 0x0000 and caches entry in TLB
    mmu::TranslationFault fault;
    auto r1 = cpu1.get_mmu().translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(r1.has_value() && r1.value() == 0x40000ULL);

    // Core 0 modifies page table: unmaps PA 0x40000 and remaps VA 0x0000 -> PA 0x60000
    mem.write_u64(0x60000, 0x9999888877776666ULL);
    mem.write_u64(l3_base, 0x60000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 11u));

    // Core 0 executes TLBI VAE1IS to invalidate locally and broadcast
    cpu0.get_mmu().invalidate_tlb_va(0x0000, 1, false);
    cluster.broadcast_tlbi_va(0, 0x0000, 1, false);

    // Now Core 1 translates VA 0x0000 again
    auto r2 = cpu1.get_mmu().translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault);
    TEST_ASSERT(r2.has_value() && r2.value() == 0x60000ULL);
    // Core 1 observes new physical page, stale translation to 0x40000 cannot be reached
    TEST_ASSERT(mem.read_u64(r2.value()) == 0x9999888877776666ULL);

    std::cout << "  -> PASSED\n";
}

void test_barrier_completion_pipeline() {
    std::cout << "[Test 4] DSB ISH and ISB execution in instruction stream...\n";
    CPU cpu;
    Memory mem(1024 * 1024);

    // Instruction sequence:
    // 0x1000: D508831F -> TLBI ALLE1IS (op1=0, CRn=8, CRm=3, op2=4, rt=31)
    // 0x1004: D5033BBF -> DSB ISH (op1=0, CRn=7, CRm=11, op2=4)
    // 0x1008: D5033FDF -> ISB (op1=0, CRn=7, CRm=4, op2=4)
    // 0x100C: D503201F -> NOP
    mem.write_u32(0x1000, 0xD508831Fu); // TLBI ALLE1IS
    mem.write_u32(0x1004, 0xD5033BBFu); // DSB ISH
    mem.write_u32(0x1008, 0xD5033FDFu); // ISB
    mem.write_u32(0x100C, 0xD503201Fu); // NOP

    cpu.set_program_counter(0x1000);
    // Set EL1 so TLBI ALLE1IS is permitted
    cpu.set_current_el(1);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1004);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x1008);

    cpu.step(mem);
    TEST_ASSERT(cpu.get_program_counter() == 0x100C);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << "Stage 198.3: Cross-Core Translation & Memory Tests\n";
    std::cout << "====================================================\n";

    test_cross_core_tlbi_broadcast();
    test_asid_isolation();
    test_page_reuse_and_stale_tlb_invalidation();
    test_barrier_completion_pipeline();

    std::cout << "\nAll Stage 198.3 Cross-Core Translation tests passed successfully!\n";
    return 0;
}
