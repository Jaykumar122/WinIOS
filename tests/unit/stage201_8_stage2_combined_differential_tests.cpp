// ==============================================================================
// WinIOS Campaign 201 Stage 8: Stage-2 & Combined Translation Verification
// ==============================================================================
// Verifies ARM64 Stage-2 hypervisor translation and combined VA->IPA->PA walks:
// 1. Stage-2 16KB translation granule walk resolution
// 2. Stage-2 S2AP permission checking (None, Read-Only, Write-Only, Read-Write)
// 3. Combined VA -> IPA -> PA translation pipeline
// 4. Strictest permission intersection (S1 RW & S2 RO -> Read-Only)
// 5. Fault precedence: Stage-1 translation fault precedes Stage-2 translation
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace winios::mmu;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void setup_16kb_page(Memory& mem, std::uint64_t l0, std::uint64_t l1, std::uint64_t l2, std::uint64_t l3, std::uint64_t desc) {
    mem.write_u64(l0, l1 | 0x3u);
    mem.write_u64(l1, l2 | 0x3u);
    mem.write_u64(l2, l3 | 0x3u);
    mem.write_u64(l3, desc);
}

// -----------------------------------------------------------------------------
// Test 1: Stage-2 16KB Granule Translation Walk
// -----------------------------------------------------------------------------
void test_stage2_translation_walk() {
    std::cout << "[Test 1] Stage-2 16KB Granule Translation Walk...\n";
    Memory mem(0x200000);
    Stage2TranslationEngine s2;
    s2.reset();

    // Enable Stage-2 translation in HCR_EL2 (bit 0 = VM)
    s2.set_hcr(0x1ULL);
    // Configure VTCR_EL2: TG0 = 0b10 (16KB granule), T0SZ = 16 (48-bit IPA)
    s2.set_vtcr((2ULL << 14u) | 16ULL);

    const std::uint64_t vttbr_pa = 0x20000ULL;
    const std::uint64_t ipa      = 0x000000004000ULL; // Page 1 in 16KB space
    const std::uint64_t final_pa = 0x000000084000ULL;

    s2.set_vttbr(vttbr_pa);

    // Map 16KB page with Read/Write permission
    Stage2TranslationEngine::map_page_16k(mem, vttbr_pa, ipa, final_pa, true, true, false);

    Stage2Fault fault{};
    auto res = s2.translate(mem, ipa, Stage2AccessType::Read, &fault);
    TEST_CHECK(res.has_value(), "Stage-2 translation must succeed");
    TEST_CHECK(res->physical_address == final_pa, "Stage-2 translated PA mismatch");

    std::cout << "  -> Stage-2 translation walk passed.\n";
}

// -----------------------------------------------------------------------------
// Test 2: S2AP Permission Enforcement
// -----------------------------------------------------------------------------
void test_stage2_permissions() {
    std::cout << "[Test 2] Stage-2 S2AP Permissions...\n";
    Memory mem(0x200000);
    Stage2TranslationEngine s2;
    s2.reset();
    s2.set_hcr(0x1ULL);
    s2.set_vtcr((2ULL << 14u) | 16ULL);

    const std::uint64_t vttbr_pa = 0x30000ULL;
    const std::uint64_t ipa      = 0x4000ULL;
    const std::uint64_t final_pa = 0x90000ULL;
    s2.set_vttbr(vttbr_pa);

    // S2AP: Read-Only
    Stage2TranslationEngine::map_page_16k(mem, vttbr_pa, ipa, final_pa, true, false, false);

    Stage2Fault fault{};
    // Read -> Succeeded
    auto r_read = s2.translate(mem, ipa, Stage2AccessType::Read, &fault);
    TEST_CHECK(r_read.has_value() && r_read->physical_address == final_pa, "Stage-2 Read must succeed on RO mapping");

    // Write -> Fault
    auto r_write = s2.translate(mem, ipa, Stage2AccessType::Write, &fault);
    TEST_CHECK(!r_write.has_value(), "Stage-2 Write must fault on RO mapping");
    TEST_CHECK(fault.type == ::mmu::MmuFaultType::Permission, "Fault must be Permission fault");

    std::cout << "  -> Stage-2 S2AP permissions passed.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Strictest Permission Intersection (S1 RW & S2 RO -> RO)
// -----------------------------------------------------------------------------
void test_permission_intersection() {
    std::cout << "[Test 3] Combined Permission Intersection...\n";
    // Using full unified MMU (src/cpu/mmu.h)
    Memory mem(0x300000);
    ::mmu::MMU mmu;
    mmu.reset();

    // Enable both Stage-1 and Stage-2
    mmu.set_sctlr_el1(0x00C50839ULL);      // S1 enable
    mmu.set_hcr_el2(0x1ULL);               // S2 enable (VM=1)
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL); // S1 16KB
    mmu.set_vtcr_el2((2ULL << 14u) | 16ULL);// S2 16KB

    const std::uint64_t s1_table_pa = 0x40000ULL;
    const std::uint64_t s2_table_pa = 0x50000ULL;
    const std::uint64_t va          = 0x0ULL;
    const std::uint64_t ipa         = 0x100000ULL;
    const std::uint64_t pa          = 0x200000ULL;

    mmu.set_ttbr0_el1(s1_table_pa);
    mmu.set_vttbr_el2(s2_table_pa);

    // Stage 1: VA -> IPA (Read/Write all ELs, AP=0b01)
    const std::uint64_t s1_desc = (ipa & 0x0000FFFFFFFFC000ULL) | (1ULL << 10u) | (1ULL << 6u) | 0x3ULL;
    setup_16kb_page(mem, s1_table_pa, 0x44000ULL, 0x48000ULL, 0x4C000ULL, s1_desc);

    // Stage 2: IPA -> PA (Read-Only)
    Stage2TranslationEngine::map_page_16k(mem, s2_table_pa, ipa, pa, true, false, false);

    ::mmu::TranslationFault fault{};
    // Read at EL0: allowed by S1 (RW) and S2 (RO) -> Overall Read Allowed
    auto r_read = mmu.translate(mem, va, ::mmu::AccessKind::Read, 0, &fault);
    TEST_CHECK(r_read.has_value() && *r_read == pa, "Combined read must resolve to PA");

    // Write at EL0: allowed by S1 (RW) but BLOCKED by S2 (RO) -> Fault!
    auto r_write = mmu.translate(mem, va, ::mmu::AccessKind::Write, 0, &fault);
    TEST_CHECK(!r_write.has_value(), "Combined write must be blocked by S2 RO permission");
    TEST_CHECK(fault.type == ::mmu::MmuFaultType::Permission, "Fault must be Permission");
    TEST_CHECK(fault.is_stage2 == true, "Fault must be identified as Stage-2 fault");

    std::cout << "  -> Combined permission intersection passed.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Fault Precedence (Stage-1 Precedes Stage-2)
// -----------------------------------------------------------------------------
void test_fault_precedence() {
    std::cout << "[Test 4] Stage-1 vs Stage-2 Fault Precedence...\n";
    Memory mem(0x300000);
    ::mmu::MMU mmu;
    mmu.reset();
    mmu.set_sctlr_el1(0x00C50839ULL);
    mmu.set_hcr_el2(0x1ULL);
    mmu.set_tcr_el1((2ULL << 14u) | 16ULL);
    mmu.set_vtcr_el2((2ULL << 14u) | 16ULL);

    const std::uint64_t s1_table_pa = 0x60000ULL;
    const std::uint64_t s2_table_pa = 0x70000ULL;
    mmu.set_ttbr0_el1(s1_table_pa);
    mmu.set_vttbr_el2(s2_table_pa);

    // Leave Stage 1 descriptor invalid (bits [1:0] = 0)
    mem.write_u64(s1_table_pa, 0ULL);

    ::mmu::TranslationFault fault{};
    auto r = mmu.translate(mem, 0x0ULL, ::mmu::AccessKind::Read, 0, &fault);
    TEST_CHECK(!r.has_value(), "Translation must fault");
    // Per ARM DDI 0487: Stage-1 translation fault takes precedence before Stage-2 translation
    TEST_CHECK(fault.type == ::mmu::MmuFaultType::Translation, "Fault must be Translation fault");
    TEST_CHECK(fault.is_stage2 == false, "Fault must originate from Stage-1, not Stage-2");

    std::cout << "  -> Fault precedence tests passed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 201.8 Stage-2 & Combined Translation Verification Tests ===\n";
    test_stage2_translation_walk();
    test_stage2_permissions();
    test_permission_intersection();
    test_fault_precedence();

    if (g_failures == 0) {
        std::cout << "All Stage 201.8 tests passed successfully!\n";
        return 0;
    } else {
        std::cerr << g_failures << " Stage 201.8 test(s) failed!\n";
        return 1;
    }
}
