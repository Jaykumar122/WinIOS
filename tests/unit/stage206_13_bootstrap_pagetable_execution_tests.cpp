#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

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

void test_identity_and_higher_half_pagetables() {
    std::cout << "[Test 1] 16KB Granule Bootstrap Page-Table Translation...\n";

    Memory memory(0x400000); // 4 MB physical RAM
    MMU mmu;

    // Set up 16KB Granule TCR_EL1:
    // TG0 = 0b10 (16KB), TG1 = 0b01 (16KB), T0SZ = 25, T1SZ = 25
    constexpr std::uint64_t kTcr16K = (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL;
    mmu.set_tcr_el1(kTcr16K);

    constexpr std::uint64_t kTtbr0Base = 0x20000ULL;
    constexpr std::uint64_t kTtbr1Base = 0x24000ULL;

    mmu.set_ttbr0_el1(kTtbr0Base);
    mmu.set_ttbr1_el1(kTtbr1Base);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL); // Enable Stage 1 MMU (M bit)

    // Populate Level 2 table in TTBR0 for identity mapping:
    // Virtual 0x00000000..0x003FFFFF -> Physical 0x00000000..0x003FFFFF
    // Descriptor format: Valid (bit 0=1), Block (bit 1=0), AF (bit 10=1), SH=Inner (3<<8), AP=EL1 RW (0<<6)
    // Value: 0x0000000000000701ULL
    memory.write_u64(static_cast<std::size_t>(kTtbr0Base), 0x0000000000000701ULL);

    // Translate Identity VA at EL1: 0x1000
    TranslationFault fault{};
    auto pa_opt = mmu.translate(memory, 0x1000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(pa_opt.has_value(), "Identity mapping translation must succeed");
    if (pa_opt) {
        TEST_CHECK(*pa_opt == 0x1000ULL, "Physical address must match virtual address (0x1000)");
    }

    // Populate Level 2 table in TTBR1 for Higher-Half kernel mapping:
    // Virtual higher-half -> physical 0x00000000
    memory.write_u64(static_cast<std::size_t>(kTtbr1Base), 0x0000000000000701ULL);

    // Translate Higher-Half VA (canonical 39-bit higher-half with T1SZ=25):
    constexpr std::uint64_t kHigherHalfVa = 0xFFFFFF8000001000ULL;
    auto pa_hh = mmu.translate(memory, kHigherHalfVa, AccessKind::Read, 1, &fault);
    TEST_CHECK(pa_hh.has_value(), "Higher-half translation must succeed");
    if (pa_hh) {
        TEST_CHECK(*pa_hh == 0x1000ULL, "Physical address matches block translation (0x1000)");
    }

    // Test Unmapped Address translation fault
    constexpr std::uint64_t kUnmappedVa = 0x0000000080000000ULL;
    auto pa_unmapped = mmu.translate(memory, kUnmappedVa, AccessKind::Read, 1, &fault);
    TEST_CHECK(!pa_unmapped.has_value(), "Unmapped address must fail translation");
    TEST_CHECK(fault.type == MmuFaultType::Translation, "Must report translation fault");

    std::cout << "  -> 16KB granule page-table translations verified.\n";
}

void test_page_permission_transitions() {
    std::cout << "[Test 2] Page Permission Transitions & W^X Enforcement...\n";

    Memory memory(0x400000);
    MMU mmu;

    constexpr std::uint64_t kTcr16K = (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL;
    mmu.set_tcr_el1(kTcr16K);
    constexpr std::uint64_t kTtbr0Base = 0x30000ULL;
    mmu.set_ttbr0_el1(kTtbr0Base);
    mmu.set_sctlr_el1(mmu.get_sctlr_el1() | 1ULL);

    // Read-only block: AP[2:1] = 0b10 (bit 7 = 1) -> Read-only at EL1
    // Valid(1), Block(0), AttrIndx(0), AP=RO(1<<7), SH=Inner(3<<8), AF(1<<10) = 0x781ULL
    memory.write_u64(static_cast<std::size_t>(kTtbr0Base), 0x0000000000000781ULL);

    TranslationFault fault{};
    // Read at EL1 must succeed
    auto pa_ro = mmu.translate(memory, 0x2000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(pa_ro.has_value(), "Read from RO page must succeed");

    // Write at EL1 to RO page must fail with Permission Fault
    auto pa_wr = mmu.translate(memory, 0x2000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!pa_wr.has_value(), "Write to RO page must fail");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault must be Permission Fault");

    std::cout << "  -> Permission transitions verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.13 Bootstrap Page-Table Execution Tests ===\n";
    test_identity_and_higher_half_pagetables();
    test_page_permission_transitions();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.13 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.13 Tests FAILED ===\n";
        return 1;
    }
}
