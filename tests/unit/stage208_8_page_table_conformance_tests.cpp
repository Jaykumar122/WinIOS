#include <iostream>
#include <cstdint>

#include "cpu/mmu.h"

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

void test_16kb_page_table_conformance() {
    std::cout << "[Test 1] Stage 208.8: 16KB Granule Descriptor Conformance...\n";

    MMU mmu;

    // 1. Validate Granule Type settings
    // TCR_EL1: TG0 = 0b10 (16KB), TG1 = 0b01 (16KB)
    const std::uint64_t tcr_16k = (0b10ULL << 14u) | (0b01ULL << 30u);
    mmu.set_tcr_el1(tcr_16k);
    TEST_CHECK(mmu.get_tg0_granule() == GranuleType::Granule16KB, "TG0 configured for 16KB");
    TEST_CHECK(mmu.get_tg1_granule() == GranuleType::Granule16KB, "TG1 configured for 16KB");

    // 2. Validate Alignment helper
    TEST_CHECK(MMU::validate_table_alignment(0x30000ULL, GranuleType::Granule16KB), "0x30000 is 16KB aligned");
    TEST_CHECK(MMU::validate_table_alignment(0x34000ULL, GranuleType::Granule16KB), "0x34000 is 16KB aligned");
    TEST_CHECK(!MMU::validate_table_alignment(0x32000ULL, GranuleType::Granule16KB), "0x32000 is not 16KB aligned (8KB offset)");

    // 3. Test Block Descriptor 0x701 (Kernel RW, Normal, Inner Shareable, AF=1)
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;
    const std::uint64_t desc_701 = 0x0000000000000701ULL;
    const bool valid_701 = mmu.validate_descriptor_for_granule(2, desc_701, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_CHECK(valid_701, "Descriptor 0x701 is valid for Level 2 in 16KB granule");
    TEST_CHECK(kind == DescriptorKind::Block, "Descriptor 0x701 is Block type");

    // 4. Test Table Descriptor (bits [1:0] = 0b11)
    const std::uint64_t desc_table = 0x0000000000034003ULL;
    const bool valid_tbl = mmu.validate_descriptor_for_granule(1, desc_table, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_CHECK(valid_tbl, "Level 1 table pointer descriptor is valid");
    TEST_CHECK(kind == DescriptorKind::Table, "Descriptor is Table type");

    // 5. Test Invalid Descriptor (bits [1:0] = 0b00)
    const std::uint64_t desc_invalid = 0x0ULL;
    const bool valid_inv = mmu.validate_descriptor_for_granule(2, desc_invalid, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_CHECK(!valid_inv, "Descriptor 0x0 is invalid");
    TEST_CHECK(kind == DescriptorKind::Invalid, "Identified as Invalid descriptor");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.8 16KB Page-Table Conformance Tests ===\n";
    test_16kb_page_table_conformance();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.8: All 16KB page-table conformance tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.8: " << g_failures << " tests failed.\n";
        return 1;
    }
}
