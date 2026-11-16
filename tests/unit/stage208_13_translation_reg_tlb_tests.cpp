#include <iostream>
#include <cstdint>

#include "cpu/mmu.h"
#include "memory/memory.h"

using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_translation_registers_and_tlb() {
    std::cout << "[Test 1] Stage 208.13: Translation Register & TLB Invalidation Tests...\n";

    MMU mmu;
    Memory mem(0x400000);

    // 1. TTBR0 / TTBR1 register configuration
    const std::uint64_t ttbr0_val = 0x0000000000030000ULL;
    const std::uint64_t ttbr1_val = 0x0001000000034000ULL; // ASID = 1 in bits [63:48]
    mmu.set_ttbr0_el1(ttbr0_val);
    mmu.set_ttbr1_el1(ttbr1_val);

    TEST_CHECK(mmu.get_ttbr0_el1() == ttbr0_val, "TTBR0_EL1 holds root table base");
    TEST_CHECK(mmu.get_ttbr1_el1() == ttbr1_val, "TTBR1_EL1 holds kernel table base with ASID");

    // 2. MAIR_EL1 register configuration
    const std::uint64_t mair_val = 0x00000000000444FFULL;
    mmu.set_mair_el1(mair_val);
    TEST_CHECK(mmu.get_mair_attribute(0) == 0xFF, "MAIR Attr0 is 0xFF (Normal WB)");
    TEST_CHECK(mmu.get_mair_attribute(1) == 0x44, "MAIR Attr1 is 0x44 (Normal Non-Cacheable)");
    TEST_CHECK(mmu.get_mair_attribute(2) == 0x04, "MAIR Attr2 is 0x04 (Device-nGnRE)");
    TEST_CHECK(mmu.is_device_attribute(0x04), "0x04 recognized as Device memory");

    // 3. TLB hit/miss statistics
    mmu.reset_tlb_stats();
    TEST_CHECK(mmu.get_tlb_hits() == 0, "TLB hits reset to 0");
    TEST_CHECK(mmu.get_tlb_misses() == 0, "TLB misses reset to 0");

    // Set up valid translation
    mem.write_u64(0x30000ULL, 0x0000000000000701ULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};
    // First lookup -> Miss + Fill
    auto t1 = mmu.translate(mem, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(t1.has_value(), "First translation succeeded");
    TEST_CHECK(mmu.get_tlb_misses() == 1, "First lookup recorded TLB miss");

    // Second lookup -> Hit!
    auto t2 = mmu.translate(mem, 0x1004ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(t2.has_value(), "Second translation succeeded");
    TEST_CHECK(mmu.get_tlb_hits() == 1, "Second lookup recorded TLB hit");

    // 4. Test TLBI Invalidation
    mmu.invalidate_tlb_all();
    TEST_CHECK(mmu.get_tlb_entry_count() == 0, "TLB invalidated all entries");

    // Third lookup after invalidation -> Miss again!
    auto t3 = mmu.translate(mem, 0x1008ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(t3.has_value(), "Translation after invalidation succeeded");
    TEST_CHECK(mmu.get_tlb_misses() == 2, "Third lookup recorded new TLB miss");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.13 Translation Register & TLB Tests ===\n";
    test_translation_registers_and_tlb();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.13: All translation register & TLB tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.13: " << g_failures << " tests failed.\n";
        return 1;
    }
}
