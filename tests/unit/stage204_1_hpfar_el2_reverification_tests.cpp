// ==============================================================================
// WinIOS Campaign 204 Stage 204.1: HPFAR & EL2 Architectural Reverification Tests
// ==============================================================================
// Independent verification and reconciliation of:
// 1. HPFAR_EL2 field extraction: FIPA bits [39:4] holding IPA[47:12]
// 2. Mathematical proof of equivalence between:
//      (A) Field extraction followed by page shift: ((hpfar >> 4) & 0xFFFFFFFFF) << 12
//      (B) Direct arithmetic shift: (hpfar & 0x000000FFFFFFFFF0ULL) << 8
// 3. Intra-page offset preservation from FAR_EL2 for 4KB (0xFFF) and 16KB (0x3FFF)
// 4. Boundary cases: Minimum IPA page, maximum 48-bit IPA, non-aligned intermediate values
// 5. Fault classification: Translation, Access Flag, Permission across levels 0..3
// 6. ESR_EL2 and FAR_EL2 consistency with ARM DDI 0487
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "memory/memory.h"
#include "memory/stage2_mmu.h"

using namespace winios;
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

// -----------------------------------------------------------------------------
// Test 1: First-Principles Mathematical Verification of Reconstruction Formulas
// -----------------------------------------------------------------------------
void test_hpfar_mathematical_equivalence() {
    std::cout << "[Stage 204.1 Test 1] HPFAR First-Principles Formula Equivalence...\n";

    constexpr std::uint64_t kHpfarMask = 0x000000FFFFFFFFF0ULL;

    const std::uint64_t test_ipas[] = {
        0x0000000000004000ULL, // Min 16KB page
        0x0000000000001000ULL, // Min 4KB page
        0x0000000080000000ULL, // 2GB boundary
        0x0000000100000000ULL, // 4GB boundary
        0x0000123456784000ULL, // Mid-range address
        0x0000ABCD80001234ULL, // Non-zero 4KB intra-page offset (0x234)
        0x0000ABCD80003FFFULL, // Max 16KB intra-page offset (0x3FFF)
        0x0000FFFFFFFFC000ULL, // High 48-bit 16KB page
        0x0000FFFFFFFFF000ULL, // Max 48-bit 4KB page
        0x0000FFFFFFFFFFFFULL  // Highest 48-bit address
    };

    for (std::uint64_t raw_ipa : test_ipas) {
        // Step 1: Compute architectural HPFAR_EL2 from first principles:
        // ARM DDI 0487: HPFAR_EL2[39:4] = IPA[47:12]
        const std::uint64_t fipa_field = (raw_ipa >> 12u) & 0x0000000FFFFFFFFFULL;
        const std::uint64_t hpfar_val = (fipa_field << 4u);

        // Verification of storage:
        TEST_CHECK((hpfar_val & 0xFull) == 0, "HPFAR_EL2[3:0] must be RES0");
        TEST_CHECK((hpfar_val >> 40u) == 0, "HPFAR_EL2[63:40] must be RES0 for 48-bit IPA");

        // Formula A: Two-step explicit extraction (>> 4 then << 12)
        const std::uint64_t extracted_fipa = (hpfar_val & kHpfarMask) >> 4u;
        const std::uint64_t page_base_A = extracted_fipa << 12u;

        // Formula B: One-step direct shift (<< 8)
        const std::uint64_t page_base_B = (hpfar_val & kHpfarMask) << 8u;

        // Proof of identity:
        TEST_CHECK(page_base_A == page_base_B, "Formula A ((hpfar >> 4) << 12) must equal Formula B (hpfar << 8)");
        TEST_CHECK(page_base_B == (raw_ipa & ~0xFFFULL), "Reconstructed page base must equal IPA[47:12] << 12");

        // 4KB granule reconstruction using FAR_EL2
        const std::uint64_t far_4k = raw_ipa; // Stage-1 faulting VA preserves low 12 bits
        const std::uint64_t recon_4k = page_base_B | (far_4k & 0xFFFULL);
        TEST_CHECK(recon_4k == raw_ipa, "4KB reconstruction must restore exact IPA");

        // 16KB granule reconstruction:
        // Note: HPFAR_EL2 holds IPA[47:12]. For 16KB granule, IPA[13:12] are held in HPFAR_EL2!
        // FAR_EL2 holds bits [13:0] for 16KB granule.
        const std::uint64_t far_16k = raw_ipa;
        const std::uint64_t page_base_16k = page_base_B & ~0x3FFFULL;
        const std::uint64_t recon_16k = page_base_16k | (far_16k & 0x3FFFULL);
        TEST_CHECK(recon_16k == raw_ipa, "16KB reconstruction must restore exact IPA");
    }

    std::cout << "  -> HPFAR mathematical equivalence independently proven.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Syndrome Report Generation and ESR/FAR Consistency
// -----------------------------------------------------------------------------
void test_stage2_syndrome_full_matrix() {
    std::cout << "[Stage 204.1 Test 2] Stage-2 Syndrome Full Fault Matrix...\n";

    // Test across all combinations of:
    // Fault Type: Translation (0x04), Access Flag (0x08), Permission (0x0C)
    // Level: 0, 1, 2, 3
    // Access: Read (WnR=0), Write (WnR=1), Execute (EC=0x20)
    struct FaultCombo {
        ::mmu::MmuFaultType type;
        unsigned level;
        Stage2AccessType access;
        std::uint32_t expected_fsc;
        std::uint32_t expected_ec;
        bool is_write;
        bool is_exec;
    };

    std::vector<FaultCombo> combos;
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        combos.push_back({::mmu::MmuFaultType::Translation, lvl, Stage2AccessType::Read, 0x04u + lvl, 0x24u, false, false});
        combos.push_back({::mmu::MmuFaultType::Translation, lvl, Stage2AccessType::Write, 0x04u + lvl, 0x24u, true, false});
        combos.push_back({::mmu::MmuFaultType::Translation, lvl, Stage2AccessType::Execute, 0x04u + lvl, 0x20u, false, true});

        combos.push_back({::mmu::MmuFaultType::AccessFlag, lvl, Stage2AccessType::Read, 0x08u + lvl, 0x24u, false, false});
        combos.push_back({::mmu::MmuFaultType::AccessFlag, lvl, Stage2AccessType::Write, 0x08u + lvl, 0x24u, true, false});

        combos.push_back({::mmu::MmuFaultType::Permission, lvl, Stage2AccessType::Read, 0x0Cu + lvl, 0x24u, false, false});
        combos.push_back({::mmu::MmuFaultType::Permission, lvl, Stage2AccessType::Write, 0x0Cu + lvl, 0x24u, true, false});
    }

    constexpr std::uint64_t kTestVA = 0x00000001ABCD4567ULL;
    constexpr std::uint64_t kTestIPA = 0x0000000088884567ULL;

    for (const auto& c : combos) {
        Stage2Fault fault{};
        fault.type = c.type;
        fault.level = c.level;
        fault.ipa = kTestIPA;
        fault.access = c.access;
        fault.is_write = c.is_write;
        fault.is_execute = c.is_exec;

        auto rep = make_stage2_syndrome_report(fault, kTestVA);

        // Check FAR_EL2
        TEST_CHECK(rep.far_el2 == kTestVA, "FAR_EL2 must reflect faulting VA");

        // Check HPFAR_EL2
        const std::uint64_t expected_hpfar = (kTestIPA >> 8u) & 0x000000FFFFFFFFF0ULL;
        TEST_CHECK(rep.hpfar_el2 == expected_hpfar, "HPFAR_EL2 must match architectural bitfield");

        // Check ESR_EL2 fields:
        const std::uint32_t actual_ec = static_cast<std::uint32_t>((rep.esr_el2 >> 26u) & 0x3Fu);
        const std::uint32_t actual_il = static_cast<std::uint32_t>((rep.esr_el2 >> 25u) & 0x1u);
        const std::uint32_t actual_iss = static_cast<std::uint32_t>(rep.esr_el2 & 0x01FFFFFFu);
        const std::uint32_t actual_fsc = actual_iss & 0x3Fu;
        const bool actual_wnr = ((actual_iss >> 6u) & 0x1u) != 0;

        TEST_CHECK(actual_ec == c.expected_ec, "EC must match instruction or data abort class");
        TEST_CHECK(actual_il == 1u, "IL bit 25 must be 1 for 32-bit instruction syndicate");
        TEST_CHECK(actual_fsc == c.expected_fsc, "FSC must match fault status code");
        if (!c.is_exec) {
            TEST_CHECK(actual_wnr == c.is_write, "WnR bit 6 must accurately represent write vs read");
        } else {
            TEST_CHECK(!actual_wnr, "WnR must be 0 for instruction abort");
        }
    }

    std::cout << "  -> Stage-2 syndrome matrix validated.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.1 HPFAR Reverification Tests ===\n";
    test_hpfar_mathematical_equivalence();
    test_stage2_syndrome_full_matrix();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.1 HPFAR Reverification passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.1 HPFAR Reverification failed with " << g_failures << " errors.\n";
        return 1;
    }
}
