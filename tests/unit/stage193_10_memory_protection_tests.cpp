// ============================================================================
// Stage 193.10: EL0/EL1 Memory Protection Tests
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

static bool test_el0_access_to_el1_only_memory_rejected() {
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
    const std::uint64_t user_code_pa = 0x30000ULL;
    const std::uint64_t kernel_data_pa = 0x50000ULL;

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0 (VA 0x0000): User code page (AP=01, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), user_code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Page 1 (VA 0x4000): Kernel-only data page (AP=00, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), kernel_data_pa | (1ULL << 10u) | 0x3ULL);

    // Write sentinel to kernel memory
    constexpr std::uint64_t kKernelSecret = 0xCAFEBABE11223344ULL;
    mem.write_u64(static_cast<std::size_t>(kernel_data_pa + 0x100), kKernelSecret);

    // User code at VA 0x1000:
    // 0x1000: LDR X0, [X1] (0xF9400020u)
    // 0x1004: STR X0, [X1] (0xF9000020u)
    const std::uint64_t user_code_va = 0x1000ULL;
    const std::uint64_t kernel_target_va = 0x4100ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_pa + 0x1000), 0xF9400020u); // LDR
    mem.write_u32(static_cast<std::size_t>(user_code_pa + 0x1004), 0xF9000020u); // STR

    // 1. EL0 Read from Kernel Memory: must trap with DataAbortLower
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, kernel_target_va);
    cpu.write_register(0, 0);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1 on unauthorized EL0 read");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == user_code_va, "ELR_EL1 must point to LDR");
    CHECK(cpu.get_far_el1() == kernel_target_va, "FAR_EL1 must hold kernel address");
    auto esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x24u, "EC must be DataAbortLower (0x24)");
    CHECK((esr & 0x3Fu) == 0x0Fu, "DFSC must be Permission fault level 3 (0x0F)");
    CHECK(((esr >> 6u) & 1u) == 0u, "WnR must be 0 for load");
    CHECK(cpu.read_register(0) == 0, "X0 must not leak kernel secret");

    // 2. EL0 Write to Kernel Memory: must trap with DataAbortLower (WnR = 1)
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code_va + 4u); // Point to STR
    cpu.write_register(1, kernel_target_va);
    cpu.write_register(0, 0xBAD00BADULL);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1 on unauthorized EL0 write");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == user_code_va + 4u, "ELR_EL1 must point to STR");
    esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x24u, "EC must be DataAbortLower (0x24)");
    CHECK((esr & 0x3Fu) == 0x0Fu, "DFSC must be Permission fault level 3 (0x0F)");
    CHECK(((esr >> 6u) & 1u) == 1u, "WnR must be 1 for store");
    CHECK(mem.read_u64(static_cast<std::size_t>(kernel_data_pa + 0x100)) == kKernelSecret, "Kernel memory must be uncorrupted");

    std::cout << "[PASS] test_el0_access_to_el1_only_memory_rejected\n";
    return true;
}

static bool test_el1_access_to_permitted_kernel_memory_succeeds() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    MMU& mmu = cpu.get_mmu();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t kernel_code_pa = 0x30000ULL;
    const std::uint64_t kernel_data_pa = 0x50000ULL;

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0 (VA 0x0000): Kernel code page (AP=00, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), kernel_code_pa | (1ULL << 10u) | 0x3ULL);
    // Page 1 (VA 0x4000): Kernel data page (AP=00, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), kernel_data_pa | (1ULL << 10u) | 0x3ULL);

    // Write initial data
    mem.write_u64(static_cast<std::size_t>(kernel_data_pa + 0x100), 0x1234567890ABCDEFULL);

    // Code at VA 0x1000:
    // 0x1000: LDR X0, [X1] (0xF9400020u)
    // 0x1004: STR X2, [X1] (0xF9000022u)
    const std::uint64_t kernel_code_va = 0x1000ULL;
    const std::uint64_t kernel_data_va = 0x4100ULL;
    mem.write_u32(static_cast<std::size_t>(kernel_code_pa + 0x1000), 0xF9400020u); // LDR
    mem.write_u32(static_cast<std::size_t>(kernel_code_pa + 0x1004), 0xF9000022u); // STR

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(kernel_code_va);
    cpu.write_register(1, kernel_data_va);
    cpu.write_register(2, 0x9988776655443322ULL); // New value to store

    // Step 1: LDR
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == kernel_code_va + 4u, "PC must increment to 0x1004");
    CHECK(cpu.read_register(0) == 0x1234567890ABCDEFULL, "EL1 must read kernel data");

    // Step 2: STR
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == kernel_code_va + 8u, "PC must increment to 0x1008");
    CHECK(mem.read_u64(static_cast<std::size_t>(kernel_data_pa + 0x100)) == 0x9988776655443322ULL, "EL1 must write kernel data");

    std::cout << "[PASS] test_el1_access_to_permitted_kernel_memory_succeeds\n";
    return true;
}

static bool test_read_only_protection_at_el0_and_el1() {
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
    const std::uint64_t code_pa = 0x30000ULL;
    const std::uint64_t ro_data_pa = 0x50000ULL;

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Code page: AP=01 (user r/w), AF=1
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Data page: AP=11 (user & kernel read-only: bit 7=1, bit 6=1), AF=1
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), ro_data_pa | (1ULL << 10u) | (1ULL << 7u) | (1ULL << 6u) | 0x3ULL);

    constexpr std::uint64_t kROVal = 0xFEEDFACE55667788ULL;
    mem.write_u64(static_cast<std::size_t>(ro_data_pa + 0x200), kROVal);

    // Code at VA 0x1000:
    // 0x1000: LDR X0, [X1] (0xF9400020u)
    // 0x1004: STR X2, [X1] (0xF9000022u)
    const std::uint64_t code_va = 0x1000ULL;
    const std::uint64_t ro_data_va = 0x4200ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1000), 0xF9400020u); // LDR
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1004), 0xF9000022u); // STR

    // --- 1. EL0 Test ---
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(code_va);
    cpu.write_register(1, ro_data_va);
    cpu.write_register(0, 0);

    // EL0 LDR succeeds
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == code_va + 4u, "EL0 LDR must advance PC");
    CHECK(cpu.read_register(0) == kROVal, "EL0 must read RO memory");

    // EL0 STR fails with DataAbortLower
    cpu.write_register(2, 0x1111111111111111ULL);
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must trap to EL1 on EL0 write to RO page");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Lower EL Sync vector");
    auto esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x24u, "EC must be DataAbortLower");
    CHECK((esr & 0x3Fu) == 0x0Fu, "DFSC must be Permission level 3");
    CHECK(((esr >> 6u) & 1u) == 1u, "WnR must be 1");

    // --- 2. EL1 Test ---
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_va);
    cpu.write_register(1, ro_data_va);
    cpu.write_register(0, 0);

    // EL1 LDR succeeds
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == code_va + 4u, "EL1 LDR must advance PC");
    CHECK(cpu.read_register(0) == kROVal, "EL1 must read RO memory");

    // EL1 STR fails with DataAbortSame
    cpu.write_register(2, 0x2222222222222222ULL);
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "Current EL SPx Sync vector");
    esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x25u, "EC must be DataAbortSame");
    CHECK((esr & 0x3Fu) == 0x0Fu, "DFSC must be Permission level 3");
    CHECK(((esr >> 6u) & 1u) == 1u, "WnR must be 1");
    CHECK(mem.read_u64(static_cast<std::size_t>(ro_data_pa + 0x200)) == kROVal, "RO memory must be untouched");

    std::cout << "[PASS] test_read_only_protection_at_el0_and_el1\n";
    return true;
}

static bool test_execute_never_protection_at_el0_and_el1() {
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
    const std::uint64_t uxn_page_pa = 0x30000ULL;
    const std::uint64_t pxn_page_pa = 0x34000ULL;

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0 (VA 0x0000): UXN=1 (bit 54=1), AP=01 (user r/w), AF=1
    const std::uint64_t uxn_desc = uxn_page_pa | (1ULL << 54u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), uxn_desc);

    // Page 1 (VA 0x4000): PXN=1 (bit 53=1), AP=00 (kernel r/w), AF=1
    const std::uint64_t pxn_desc = pxn_page_pa | (1ULL << 53u) | (1ULL << 10u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), pxn_desc);

    // Put valid instruction at both pages: NOP (0xD503201Fu)
    mem.write_u32(static_cast<std::size_t>(uxn_page_pa + 0x1000), 0xD503201Fu);
    mem.write_u32(static_cast<std::size_t>(pxn_page_pa + 0x1000), 0xD503201Fu);

    // 1. EL0 execution on UXN page: must trap with InstructionAbortLower
    const std::uint64_t uxn_code_va = 0x1000ULL;
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(uxn_code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1 on UXN execution");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == uxn_code_va, "ELR_EL1 must point to UXN instruction address");
    CHECK(cpu.get_far_el1() == uxn_code_va, "FAR_EL1 must point to UXN instruction address");
    auto esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x20u, "EC must be InstructionAbortLower (0x20)");
    CHECK((esr & 0x3Fu) == 0x0Fu, "IFSC must be Permission fault level 3 (0x0F)");

    // 2. EL1 execution on PXN page: must trap with InstructionAbortSame
    const std::uint64_t pxn_code_va = 0x5000ULL;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(pxn_code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "Current EL SPx Sync vector");
    CHECK(cpu.get_elr_el1() == pxn_code_va, "ELR_EL1 must point to PXN instruction address");
    CHECK(cpu.get_far_el1() == pxn_code_va, "FAR_EL1 must point to PXN instruction address");
    esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x21u, "EC must be InstructionAbortSame (0x21)");
    CHECK((esr & 0x3Fu) == 0x0Fu, "IFSC must be Permission fault level 3 (0x0F)");

    std::cout << "[PASS] test_execute_never_protection_at_el0_and_el1\n";
    return true;
}

static bool test_wxn_policy_enforcement() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    MMU& mmu = cpu.get_mmu();
    // Enable MMU and enable WXN (bit 19): 0x00C50839 | (1 << 19)
    mmu.set_sctlr_el1(0x00C50839ULL | (1ULL << 19u));
    CHECK(mmu.is_wxn_enabled(), "WXN must be enabled");
    mmu.set_tcr_el1((2ULL << 14u) | 28ULL);

    const std::uint64_t l2_pa = 0x40000ULL;
    const std::uint64_t l3_pa = 0x48000ULL;
    const std::uint64_t writable_code_pa = 0x30000ULL;

    mmu.set_ttbr0_el1((1ULL << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Page 0: Writable page (AP=01, read_only=false), UXN=0, PXN=0, AF=1
    // Under WXN, writable implies eXecute-Never!
    const std::uint64_t rw_desc = writable_code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), rw_desc);
    mem.write_u32(static_cast<std::size_t>(writable_code_pa + 0x1000), 0xD503201Fu); // NOP

    const std::uint64_t code_va = 0x1000ULL;

    // Attempt to execute from writable page at EL0 under WXN
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(code_va);

    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must trap to EL1 under WXN on writable page execution");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Lower EL Sync vector");
    auto esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x20u, "EC must be InstructionAbortLower");
    CHECK((esr & 0x3Fu) == 0x0Fu, "IFSC must be Permission fault level 3");

    std::cout << "[PASS] test_wxn_policy_enforcement\n";
    return true;
}

static bool test_dynamic_permission_changes_and_tlb_synchronization() {
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
    const std::uint64_t code_pa = 0x30000ULL;
    const std::uint64_t data_pa = 0x50000ULL;

    const std::uint16_t asid = 1;
    mmu.set_ttbr0_el1((static_cast<std::uint64_t>(asid) << 48u) | l2_pa);
    mem.write_u64(static_cast<std::size_t>(l2_pa), l3_pa | 0x3ULL);

    // Code page (AP=01, AF=1)
    mem.write_u64(static_cast<std::size_t>(l3_pa + 0 * 8), code_pa | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL);
    // Data page starts as Read-Only (AP=11, bit 7=1, bit 6=1, AF=1, nG=1)
    const std::uint64_t ro_desc = data_pa | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 7u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), ro_desc);

    mem.write_u64(static_cast<std::size_t>(data_pa + 0x300), 0ULL);

    // Code at 0x1000: STR X2, [X1] (0xF9000022u)
    const std::uint64_t code_va = 0x1000ULL;
    const std::uint64_t data_va = 0x4300ULL;
    mem.write_u32(static_cast<std::size_t>(code_pa + 0x1000), 0xF9000022u); // STR

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(code_va);
    cpu.write_register(1, data_va);
    cpu.write_register(2, 0x445566778899AABBULL);

    // Attempt STR: traps because page is Read-Only
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must trap on initial RO store");
    CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "Trapped to exception vector");

    // Kernel handles fault: upgrades page to Read-Write in page table
    const std::uint64_t rw_desc = data_pa | (1ULL << 11u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    mem.write_u64(static_cast<std::size_t>(l3_pa + 1 * 8), rw_desc);

    // Kernel invalidates stale TLB entry using TLBI VAE1
    mmu.invalidate_tlb_va(data_va, asid, false);

    // Kernel returns to user code (EL0) and retries STR
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(code_va); // Retry instruction

    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must remain at EL0 on successful store");
    CHECK(cpu.get_program_counter() == code_va + 4u, "PC must advance past STR");
    CHECK(mem.read_u64(static_cast<std::size_t>(data_pa + 0x300)) == 0x445566778899AABBULL, "Store must succeed after permission upgrade");

    std::cout << "[PASS] test_dynamic_permission_changes_and_tlb_synchronization\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.10: EL0/EL1 Memory Protection Tests ===\n";
        if (!test_el0_access_to_el1_only_memory_rejected()) return 1;
        if (!test_el1_access_to_permitted_kernel_memory_succeeds()) return 1;
        if (!test_read_only_protection_at_el0_and_el1()) return 1;
        if (!test_execute_never_protection_at_el0_and_el1()) return 1;
        if (!test_wxn_policy_enforcement()) return 1;
        if (!test_dynamic_permission_changes_and_tlb_synchronization()) return 1;
        std::cout << "=== All Stage 193.10 Tests Passed (6/6) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
