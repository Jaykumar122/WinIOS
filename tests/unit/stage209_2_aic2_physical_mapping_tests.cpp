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

void test_aic2_physical_mapping_audit() {
    std::cout << "[Test 1] Stage 209.2: Apple AIC2 Physical Address Translation Audit...\n";

    MMU mmu;

    // 1. Target Virtual Address: 0x23B100000
    const std::uint64_t va_aic = 0x23B100000ULL;
    const std::uint64_t block_size = 32ULL * 1024ULL * 1024ULL; // 32 MiB = 0x2000000

    // 2. Compute Level-2 Table Index and Block-Internal Offset
    const std::size_t l2_idx = static_cast<std::size_t>(va_aic / block_size);
    const std::uint64_t offset = va_aic % block_size;

    std::cout << "  -> Level-2 Index: " << l2_idx << " (expected 285 / 0x11D)\n";
    std::cout << "  -> Block Offset: 0x" << std::hex << offset << std::dec << " (expected 0x1100000)\n";

    TEST_CHECK(l2_idx == 285, "Index matches Level-2 slot 285");
    TEST_CHECK(offset == 0x1100000ULL, "Offset within 32MB block is 0x1100000");

    // 3. Audit Previous Campaign 208 Erroneous Descriptor (0x0220000705)
    const std::uint64_t bad_desc = 0x0000000220000705ULL;
    const std::uint64_t l2_oa_mask = 0x0000FFFFFE000000ULL; // bits [47:25]
    const std::uint64_t bad_oa = bad_desc & l2_oa_mask;
    const std::uint64_t bad_translated_pa = bad_oa + offset;

    std::cout << "  -> Bad Descriptor Output Base: 0x" << std::hex << bad_oa << std::dec << "\n";
    std::cout << "  -> Bad Translated PA: 0x" << std::hex << bad_translated_pa << std::dec << "\n";

    TEST_CHECK(bad_oa == 0x0220000000ULL, "Bad descriptor base was 0x220000000");
    TEST_CHECK(bad_translated_pa == 0x0221100000ULL, "Bad translated PA was 0x221100000 (wrong by 416MB)");
    TEST_CHECK(bad_translated_pa != va_aic, "Bad descriptor fails identity mapping");

    // 4. Validate Corrected Campaign 209 Descriptor (0x000000023A000705)
    // Required base: 285 * 32MB = 0x23A000000
    const std::uint64_t correct_base = static_cast<std::uint64_t>(l2_idx) * block_size;
    TEST_CHECK(correct_base == 0x023A000000ULL, "Correct output base is 0x23A000000");

    const std::uint64_t lower_attrs = 0x705ULL; // Device-nGnRE, RW, Inner Shareable, AF=1
    const std::uint64_t corrected_desc = correct_base | lower_attrs;

    std::cout << "  -> Corrected Descriptor: 0x" << std::hex << corrected_desc << std::dec << "\n";
    TEST_CHECK(corrected_desc == 0x000000023A000705ULL, "Corrected descriptor is 0x023A000705");

    // Validate descriptor against MMU 16KB granule parser
    DescriptorKind kind = DescriptorKind::Invalid;
    MmuFaultType fault = MmuFaultType::Translation;
    const bool valid = mmu.validate_descriptor_for_granule(2, corrected_desc, GranuleType::Granule16KB, false, &kind, &fault);
    TEST_CHECK(valid, "Corrected descriptor passes ARMv8.6-A 16KB Level 2 validation");
    TEST_CHECK(kind == DescriptorKind::Block, "Identified as valid 32MB Block descriptor");

    // 5. Test Full Translation through Corrected Descriptor
    const std::uint64_t final_pa = (corrected_desc & l2_oa_mask) + offset;
    std::cout << "  -> Corrected Translated PA: 0x" << std::hex << final_pa << std::dec << "\n";
    TEST_CHECK(final_pa == va_aic, "Corrected descriptor achieves 100% bit-exact identity translation to AIC2");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.2 AIC2 Physical Address Mapping Tests ===\n";
    test_aic2_physical_mapping_audit();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.2: All AIC2 physical address mapping tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.2: " << g_failures << " tests failed.\n";
        return 1;
    }
}
