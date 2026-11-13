// ============================================================================
// Stage 193.7: MMU Translation and Fault Integration Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace mmu;

static bool test_16kb_granule_translation_and_fault_levels() {
    CPU cpu;
    Memory mem(0x400000ULL); // 4MB

    MMU& mmu = cpu.get_mmu();
    // Enable MMU via SCTLR_EL1.M (bit 0)
    mmu.set_sctlr_el1(0x00C50839ULL);

    // Setup TCR_EL1: TG0=16KB (bits 15:14 = 0b10 = 2), T0SZ=28 (36-bit VA, Apple Darwin standard userspace)
    const std::uint64_t tcr = (2ULL << 14u) | 28ULL;
    mmu.set_tcr_el1(tcr);
    CHECK(mmu.get_tg0_granule() == GranuleType::Granule16KB, "TG0 granule must be 16KB");

    // In 16KB granule with 36-bit VA (T0SZ=28), Level 2 table is the initial lookup root (16KB aligned)
    const std::uint64_t l2_table_pa = 0x40000ULL;
    mmu.set_ttbr0_el1(l2_table_pa);

    // Case A: Level 2 Translation Fault (invalid descriptor at L2 root)
    mem.write_u64(static_cast<std::size_t>(l2_table_pa), 0ULL); // Valid bit = 0

    TranslationFault fault{};
    auto res2 = mmu.translate(mem, 0x00001000ULL, AccessKind::Read, 1, &fault);
    CHECK(!res2.has_value(), "L2 invalid descriptor must fail translation");
    CHECK(fault.type == MmuFaultType::Translation, "Fault type must be Translation");
    CHECK(fault.level == 2, "Fault level must be 2");
    CHECK(fault.fsc() == 0x06u, "FSC must be 0x06 (Translation fault level 2)");

    // Case B: Level 3 Translation Fault
    // Point L2 entry 0 to an L3 table
    const std::uint64_t l3_table_pa = 0x48000ULL;
    const std::uint64_t l2_desc = l3_table_pa | 0x3ULL; // Table descriptor (type=3)
    mem.write_u64(static_cast<std::size_t>(l2_table_pa), l2_desc);
    // Clear L3 table entry 0 (invalid)
    mem.write_u64(static_cast<std::size_t>(l3_table_pa), 0ULL);

    auto res3 = mmu.translate(mem, 0x00001000ULL, AccessKind::Read, 1, &fault);
    CHECK(!res3.has_value(), "L3 invalid descriptor must fail translation");
    CHECK(fault.type == MmuFaultType::Translation, "Fault type must be Translation");
    CHECK(fault.level == 3, "Fault level must be 3");
    CHECK(fault.fsc() == 0x07u, "FSC must be 0x07 (Translation fault level 3)");

    std::cout << "[PASS] test_16kb_granule_translation_and_fault_levels\n";
    return true;
}

static bool test_access_flag_and_permission_fault_integration() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t target_pa = 0x50000ULL;

    mmu.set_ttbr0_el1(l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // 1. Access Flag Fault: L3 descriptor with AF (bit 10) = 0
    // AP=0b00, AF=0, type=3
    const std::uint64_t l3_desc_no_af = target_pa | 0x3ULL; // AF=0
    mem.write_u64(static_cast<std::size_t>(l3_pa), l3_desc_no_af);

    TranslationFault fault{};
    auto res_af = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(!res_af.has_value(), "AF=0 must cause fault");
    CHECK(fault.type == MmuFaultType::AccessFlag, "Must be AccessFlag fault");
    CHECK(fault.fsc() == 0x0Bu, "FSC must be 0x0B (Access flag fault level 3)");

    // 2. Permission Fault: Read-only page (AP[2:1] = 0b10, bit 7 = 1) with AF=1 (bit 10 = 1)
    const std::uint64_t l3_desc_ro = target_pa | (1ULL << 10u) | (1ULL << 7u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa), l3_desc_ro);

    // Verify Read succeeds
    auto res_rd = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    CHECK(res_rd.has_value() && *res_rd == target_pa + 0x1000ULL, "Read must succeed on RO page");

    // Verify Write fails with Permission fault level 3 (FSC 0x0F)
    auto res_wr = mmu.translate(mem, 0x1000ULL, AccessKind::Write, 1, &fault);
    CHECK(!res_wr.has_value(), "Write must fail on RO page");
    CHECK(fault.type == MmuFaultType::Permission, "Must be Permission fault");
    CHECK(fault.level == 3, "Fault level must be 3");
    CHECK(fault.fsc() == 0x0Fu, "FSC must be 0x0F (Permission fault level 3)");

    std::cout << "[PASS] test_access_flag_and_permission_fault_integration\n";
    return true;
}

static bool test_guest_unprivileged_el0_access_to_el1_kernel_memory() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t user_code_pa = 0x30000ULL; // PA for user code page (page 0)
    const std::uint64_t kernel_data_pa = 0x50000ULL; // PA for kernel data page (page 1)

    mmu.set_ttbr0_el1(l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Entry 0 (VA 0x0000 - 0x3FFF): User code page (AP[2:1] = 0b01 -> bit 6 = 1, bit 7 = 0, AF=1)
    const std::uint64_t user_l3_desc = user_code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), user_l3_desc);

    // Entry 1 (VA 0x4000 - 0x7FFF): Kernel-only data page (AP[2:1] = 0b00 -> bit 6 = 0, bit 7 = 0, AF=1)
    const std::uint64_t kernel_l3_desc = kernel_data_pa | (1ULL << 10u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), kernel_l3_desc);

    // Put initial kernel data at kernel_data_pa + 0x100
    mem.write_u64(static_cast<std::size_t>(kernel_data_pa + 0x100), 0xCAFEBABE11223344ULL);

    // Guest EL0 code at VA 0x1000 (PA user_code_pa + 0x1000):
    // LDR X0, [X1] (0xF9400020u)
    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_pa + 0x1000), 0xF9400020u);

    // Target kernel address in page 1: VA 0x4100
    const std::uint64_t kernel_va = 0x4100ULL;

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, kernel_va);
    cpu.write_register(0, 0);

    // Step: EL0 LDR X0, [X1] fetches from 0x1000 (succeeds) and reads 0x4100 (traps with Data Abort Lower)
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "PC must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == user_code_va, "ELR_EL1 must point to faulting LDR");
    CHECK(cpu.get_far_el1() == kernel_va, "FAR_EL1 must record faulting address (0x4100)");

    const auto esr = cpu.get_esr_el1();
    const auto ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    const auto dfsc = static_cast<std::uint32_t>(esr & 0x3Fu);
    const auto wnr = static_cast<std::uint32_t>((esr >> 6u) & 0x1u);

    CHECK(ec == 0x24u, "ESR EC must be DataAbortLower (0x24)");
    CHECK(dfsc == 0x0Fu, "DFSC must be Permission fault level 3 (0x0F)");
    CHECK(wnr == 0u, "WnR must be 0 for load");
    CHECK(cpu.read_register(0) == 0, "X0 must not be modified by faulted load");

    std::cout << "[PASS] test_guest_unprivileged_el0_access_to_el1_kernel_memory\n";
    return true;
}

static bool test_guest_store_permission_fault_atomicity() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t kernel_code_pa = 0x30000ULL;
    const std::uint64_t ro_data_pa = 0x50000ULL;

    mmu.set_ttbr0_el1(l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Entry 0 (VA 0x0000 - 0x3FFF): EL1 code page (read/write/execute, AP[2:1] = 0b00, AF=1)
    const std::uint64_t code_l3_desc = kernel_code_pa | (1ULL << 10u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), code_l3_desc);

    // Entry 1 (VA 0x4000 - 0x7FFF): Read-only page (AP[2:1] = 0b10 -> bit 7 = 1, bit 6 = 1, AF=1)
    const std::uint64_t ro_l3_desc = ro_data_pa | (1ULL << 10u) | (1ULL << 7u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), ro_l3_desc);

    // Memory contains original sentinel value
    constexpr std::uint64_t kSentinel = 0xDEADBEEFCAFE0011ULL;
    mem.write_u64(static_cast<std::size_t>(ro_data_pa + 0x200), kSentinel);

    // Guest EL1 code at VA 0x1000 (PA kernel_code_pa + 0x1000): STR X0, [X1] (0xF9000020u)
    const std::uint64_t code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(kernel_code_pa + 0x1000), 0xF9000020u);

    const std::uint64_t target_va = 0x4200ULL; // In read-only page 1

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_va);
    cpu.write_register(0, 0x1111222233334444ULL); // Data to store
    cpu.write_register(1, target_va); // Virtual address

    // Step: STR on read-only page must fail with Data Abort (Same EL)
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "PC must jump to Current EL SPx Sync vector");
    CHECK(cpu.get_far_el1() == target_va, "FAR_EL1 must hold fault address (0x4200)");

    const auto esr = cpu.get_esr_el1();
    const auto ec = static_cast<std::uint32_t>((esr >> 26u) & 0x3Fu);
    const auto dfsc = static_cast<std::uint32_t>(esr & 0x3Fu);
    const auto wnr = static_cast<std::uint32_t>((esr >> 6u) & 0x1u);

    CHECK(ec == 0x25u, "ESR EC must be DataAbortSame (0x25)");
    CHECK(dfsc == 0x0Fu, "DFSC must be Permission fault level 3 (0x0F)");
    CHECK(wnr == 1u, "WnR must be 1 for store");

    // Verify all-or-nothing atomicity: original memory must be completely untouched
    const auto current_val = mem.read_u64(static_cast<std::size_t>(ro_data_pa + 0x200));
    CHECK(current_val == kSentinel, "Target memory must remain completely unmodified on aborted store");

    std::cout << "[PASS] test_guest_store_permission_fault_atomicity\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.7: MMU Translation and Fault Integration Tests ===\n";
        if (!test_16kb_granule_translation_and_fault_levels()) return 1;
        if (!test_access_flag_and_permission_fault_integration()) return 1;
        if (!test_guest_unprivileged_el0_access_to_el1_kernel_memory()) return 1;
        if (!test_guest_store_permission_fault_atomicity()) return 1;
        std::cout << "=== All Stage 193.7 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
