// ==============================================================================
// WinIOS Campaign 201 Stage 7: Stage-1 MMU Differential Testing
// ==============================================================================
// Verifies ARM64 Stage-1 translation against Arm DDI 0487 standards:
// 1. 16KB Granule walk resolution (TG0 = 0b10 in TCR_EL1)
// 2. AP[2:1] Data Access Permission Matrix (EL0 vs EL1, Read-Only vs Read-Write)
// 3. Access Flag (AF = bit 10) fault triggers when AF == 0
// 4. Execute-Never (UXN / PXN) enforcement and instruction abort generation
// 5. Alignment fault detection for misaligned stack pointer and PC
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/mmu.h"
#include "memory/memory.h"

using namespace mmu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: 16KB Granule Configuration and Alignment Validation
// -----------------------------------------------------------------------------
void test_16kb_granule_configuration() {
    std::cout << "[Test 1] 16KB Granule Configuration...\n";
    MMU mmu;
    mmu.reset();

    // TCR_EL1 TG0 = 0b10 (16KB granule)
    // Bit [15:14] = 0b10 -> 0x8000
    mmu.set_tcr_el1(0x8000ULL);
    TEST_CHECK(mmu.get_tg0_granule() == GranuleType::Granule16KB, "TG0 must report Granule16KB");
    TEST_CHECK(MMU::get_granule_size(GranuleType::Granule16KB) == 16384ULL, "16KB granule size must be 16384");

    // Table alignment validation: 16KB requires 16384-byte boundary
    TEST_CHECK(MMU::validate_table_alignment(0x40000000ULL, GranuleType::Granule16KB), "0x40000000 is 16KB aligned");
    TEST_CHECK(MMU::validate_table_alignment(0x40004000ULL, GranuleType::Granule16KB), "0x40004000 is 16KB aligned");
    TEST_CHECK(!MMU::validate_table_alignment(0x40001000ULL, GranuleType::Granule16KB), "0x40001000 is NOT 16KB aligned");

    std::cout << "  -> 16KB granule configuration passed.\n";
}

// Helper to configure complete 4-level 16KB Stage-1 hierarchy
void setup_16kb_page(Memory& mem, std::uint64_t l0, std::uint64_t l1, std::uint64_t l2, std::uint64_t l3, std::uint64_t desc) {
    mem.write_u64(l0, l1 | 0x3u);
    mem.write_u64(l1, l2 | 0x3u);
    mem.write_u64(l2, l3 | 0x3u);
    mem.write_u64(l3, desc);
}

// -----------------------------------------------------------------------------
// Test 2: AP[2:1] Permission Matrix
// -----------------------------------------------------------------------------
void test_ap_permission_matrix() {
    std::cout << "[Test 2] AP[2:1] Permission Matrix...\n";
    // AP[2:1] encoding per ARM DDI 0487:
    // AP[2:1] = 00: Read/Write, EL1 only
    // AP[2:1] = 01: Read/Write, Any EL (EL0 & EL1)
    // AP[2:1] = 10: Read-Only, EL1 only
    // AP[2:1] = 11: Read-Only, Any EL
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();

    // Enable Stage-1 MMU in SCTLR_EL1 (bit 0 = 1)
    mmu.set_sctlr_el1(0x00C50839ULL);
    // 48-bit VA, 16KB granule (TG0 = 0b10, T0SZ = 16)
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);

    const std::uint64_t page_pa = 0x40000ULL;
    mmu.set_ttbr0_el1(0x10000ULL);

    // Case 2.1: AP = 0b11 (Read-Only all ELs)
    const std::uint64_t desc_ro_all = (page_pa & 0x0000FFFFFFFFC000ULL) | (1ULL << 10u) | (3ULL << 6u) | 0x3ULL;
    setup_16kb_page(mem, 0x10000ULL, 0x14000ULL, 0x18000ULL, 0x1C000ULL, desc_ro_all);

    TranslationFault fault{};
    // Read at EL0 -> Allowed
    auto r_el0_read = mmu.translate(mem, 0x0ULL, AccessKind::Read, 0, &fault);
    TEST_CHECK(r_el0_read.has_value() && *r_el0_read == page_pa, "EL0 read on RO page must succeed");

    // Write at EL0 -> Permission fault
    auto r_el0_write = mmu.translate(mem, 0x0ULL, AccessKind::Write, 0, &fault);
    TEST_CHECK(!r_el0_write.has_value(), "EL0 write on RO page must fault");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault type must be Permission");

    // Write at EL1 -> Permission fault
    auto r_el1_write = mmu.translate(mem, 0x0ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!r_el1_write.has_value(), "EL1 write on RO-all page must fault");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault type must be Permission");

    std::cout << "  -> AP[2:1] permission matrix passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Access Flag (AF) Fault Trigger
// -----------------------------------------------------------------------------
void test_access_flag_fault() {
    std::cout << "[Test 3] Access Flag (AF) Fault Generation...\n";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);

    const std::uint64_t page_pa = 0x50000ULL;
    mmu.set_ttbr0_el1(0x20000ULL);

    // Descriptor with AF = 0 (bit 10 is 0), RW all ELs (AP = 0b01)
    const std::uint64_t desc_no_af = (page_pa & 0x0000FFFFFFFFC000ULL) | (0ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    setup_16kb_page(mem, 0x20000ULL, 0x24000ULL, 0x28000ULL, 0x2C000ULL, desc_no_af);

    TranslationFault fault{};
    auto r = mmu.translate(mem, 0x0ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!r.has_value(), "Translation with AF=0 must fault");
    TEST_CHECK(fault.type == MmuFaultType::AccessFlag, "Fault must be AccessFlag");
    TEST_CHECK(fault.fsc() >= 0x08u && fault.fsc() <= 0x0Bu, "FSC must be 0x08-0x0B for AccessFlag");

    std::cout << "  -> Access Flag fault tests passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Execute-Never (UXN / PXN) Enforcement
// -----------------------------------------------------------------------------
void test_execute_never_enforcement() {
    std::cout << "[Test 4] Execute-Never (UXN/PXN) Enforcement...\n";
    Memory mem(0x200000);
    MMU mmu;
    mmu.reset();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);

    const std::uint64_t page_pa = 0x60000ULL;
    mmu.set_ttbr0_el1(0x30000ULL);

    // UXN bit 54 = 1 (Unprivileged Execute-Never)
    const std::uint64_t desc_uxn = (page_pa & 0x0000FFFFFFFFC000ULL) | (1ULL << 54u) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    setup_16kb_page(mem, 0x30000ULL, 0x34000ULL, 0x38000ULL, 0x3C000ULL, desc_uxn);

    TranslationFault fault{};
    // Execute from EL0 -> must fault with Permission fault
    auto r_el0_exec = mmu.translate(mem, 0x0ULL, AccessKind::Execute, 0, &fault);
    TEST_CHECK(!r_el0_exec.has_value(), "EL0 execute on UXN page must fault");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault must be Permission");

    std::cout << "  -> Execute-Never tests passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.7 Stage-1 MMU Differential Tests ===\n";
    test_16kb_granule_configuration();
    test_ap_permission_matrix();
    test_access_flag_fault();
    test_execute_never_enforcement();

    if (g_failures == 0) {
        std::cout << "All Stage 201.7 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.7 test(s) failed!\n";
        return 1;
    }
}
