// ==============================================================================
// WinIOS Campaign 203 Stage 203.2: EL2 Syndrome & HPFAR Architectural Audit Tests
// ==============================================================================
// Independent verification of:
// 1. ESR_EL2 exception class (0x20 instruction abort vs 0x24 data abort)
// 2. Exact Fault Status Codes (FSC) across levels 0..3 for translation, access-flag, permission
// 3. WnR (Write-not-Read) bit 6 and IL (bit 25) encoding
// 4. HPFAR_EL2 bitfield mapping (IPA[47:12] -> HPFAR[39:4]) and exact IPA reconstruction
// 5. 48-bit IPA boundary values (min 16KB page, max 48-bit address)
// 6. Stage-2 fault injection, hypervisor syndrome decode, page remap, and ERET replay
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
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
// Test 1: ESR_EL2 Syndrome Field Layout against ARM DDI 0487 Architectural Oracles
// -----------------------------------------------------------------------------
void test_esr_el2_architectural_oracles() {
    std::cout << "[Stage 203.2 Test 1] ESR_EL2 Architectural Oracles...\n";

    // ARM DDI 0487 Reference Definitions:
    // EC = 0x24 (Data Abort), IL = 1 (bit 25) -> base = (0x24 << 26) | (1 << 25) = 0x92000000ULL
    constexpr std::uint64_t kEsrDataAbortBase = 0x92000000ULL;
    // EC = 0x20 (Insn Abort), IL = 1 (bit 25) -> base = (0x20 << 26) | (1 << 25) = 0x82000000ULL
    constexpr std::uint64_t kEsrInsnAbortBase = 0x82000000ULL;
    constexpr std::uint32_t kWnRBit           = 0x40u; // bit 6

    // 1. Data Read Translation Faults: Level 0..3
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::Translation;
        fault.level = lvl;
        fault.ipa = 0x4000ULL;
        fault.access = Stage2AccessType::Read;
        fault.is_write = false;
        fault.is_execute = false;

        auto rep = make_stage2_syndrome_report(fault, 0x10004000ULL);
        const std::uint64_t expected_esr = kEsrDataAbortBase | (0x04u + lvl); // FSC 0x04..0x07
        TEST_CHECK(rep.esr_el2 == expected_esr, "Data read translation fault ESR must match ARM specification");
    }

    // 2. Data Write Translation Faults: Level 0..3 (WnR = 1)
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::Translation;
        fault.level = lvl;
        fault.ipa = 0x4000ULL;
        fault.access = Stage2AccessType::Write;
        fault.is_write = true;
        fault.is_execute = false;

        auto rep = make_stage2_syndrome_report(fault, 0x10004000ULL);
        const std::uint64_t expected_esr = kEsrDataAbortBase | kWnRBit | (0x04u + lvl);
        TEST_CHECK(rep.esr_el2 == expected_esr, "Data write translation fault ESR must match ARM specification");
    }

    // 3. Instruction Fetch Translation Faults: Level 0..3 (EC = 0x20, WnR = 0)
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::Translation;
        fault.level = lvl;
        fault.ipa = 0x8000ULL;
        fault.access = Stage2AccessType::Execute;
        fault.is_write = false;
        fault.is_execute = true;

        auto rep = make_stage2_syndrome_report(fault, 0x20008000ULL);
        const std::uint64_t expected_esr = kEsrInsnAbortBase | (0x04u + lvl);
        TEST_CHECK(rep.esr_el2 == expected_esr, "Instruction fetch translation fault ESR must match ARM specification");
    }

    // 4. Permission Faults: FSC 0x0C..0x0F
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::Permission;
        fault.level = lvl;
        fault.ipa = 0xC000ULL;
        fault.access = Stage2AccessType::Write;
        fault.is_write = true;
        fault.is_execute = false;

        auto rep = make_stage2_syndrome_report(fault, 0x3000C000ULL);
        const std::uint64_t expected_esr = kEsrDataAbortBase | kWnRBit | (0x0Cu + lvl);
        TEST_CHECK(rep.esr_el2 == expected_esr, "Permission fault ESR must match ARM specification");
    }

    // 5. Access Flag Faults: FSC 0x08..0x0B
    for (unsigned lvl = 0; lvl <= 3; ++lvl) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::AccessFlag;
        fault.level = lvl;
        fault.ipa = 0x10000ULL;
        fault.access = Stage2AccessType::Read;
        fault.is_write = false;
        fault.is_execute = false;

        auto rep = make_stage2_syndrome_report(fault, 0x40010000ULL);
        const std::uint64_t expected_esr = kEsrDataAbortBase | (0x08u + lvl);
        TEST_CHECK(rep.esr_el2 == expected_esr, "Access flag fault ESR must match ARM specification");
    }

    std::cout << "  -> ESR_EL2 architectural oracles verified.\n";
}

// -----------------------------------------------------------------------------
// Test 2: HPFAR_EL2 Field Layout & Exact IPA Reconstruction
// -----------------------------------------------------------------------------
void test_hpfar_el2_reconstruction() {
    std::cout << "[Stage 203.2 Test 2] HPFAR_EL2 Field Layout & IPA Reconstruction...\n";

    // ARM DDI 0487 Specification:
    // HPFAR_EL2[39:4] holds bits [47:12] of the faulting IPA.
    // Bits [3:0] are RES0 (0).
    // Bits [63:40] are RES0 (0) for 48-bit IPA.
    // Thus: hpfar_el2 = (ipa[47:12] << 4) = (ipa >> 8) & 0x00000FFFFFFFF000ULL.

    struct IpaTestCase {
        std::uint64_t ipa;
        std::uint64_t expected_hpfar;
    };

    const IpaTestCase test_cases[] = {
        // Min 16KB page: 0x4000 -> ipa[47:12] = 4 -> hpfar[39:4] = 0x40
        { 0x0000000000004000ULL, 0x0000000000000040ULL },
        // Mid address
        { 0x0000123456784000ULL, 0x0000001234567840ULL },
        // High 48-bit boundary: 0x0000FFFFFFFFC000ULL
        { 0x0000FFFFFFFFC000ULL, 0x000000FFFFFFFFC0ULL },
        // Address with non-zero page offset: offset [11:0] is 0x234, IPA[47:12] = 0xABCD80001
        { 0x0000ABCD80001234ULL, 0x000000ABCD800010ULL }
    };

    for (const auto& tc : test_cases) {
        Stage2Fault fault{};
        fault.type = ::mmu::MmuFaultType::Translation;
        fault.level = 3;
        fault.ipa = tc.ipa;

        auto rep = make_stage2_syndrome_report(fault, 0ULL);
        TEST_CHECK(rep.hpfar_el2 == tc.expected_hpfar, "HPFAR_EL2 must match architectural bitfield expectation");

        // Independent IPA Reconstruction:
        // Page frame IPA = (HPFAR_EL2 & 0x000000FFFFFFFFF0ULL) << 8
        // Page offset = tc.ipa & 0xFFFULL (4KB page offset in FIPA)
        const std::uint64_t reconstructed_page = (rep.hpfar_el2 & 0x000000FFFFFFFFF0ULL) << 8u;
        const std::uint64_t reconstructed_ipa = reconstructed_page | (tc.ipa & 0xFFFULL);
        TEST_CHECK(reconstructed_ipa == tc.ipa, "Reconstructed IPA must bit-for-bit match original faulting IPA");
    }

    std::cout << "  -> HPFAR_EL2 field layout & reconstruction verified.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Stage-2 Fault Injection, Hypervisor Trap, Remap & Replay Lifecycle
// -----------------------------------------------------------------------------
void test_fault_remap_replay_lifecycle() {
    std::cout << "[Stage 203.2 Test 3] Stage-2 Fault Remap & Replay Lifecycle...\n";

    Memory mem(0x1000000ULL); // 16MB physical RAM
    Stage2TranslationEngine s2;
    const std::uint64_t vttbr_base = 0x10000ULL;
    s2.set_vttbr((1ULL << 48u) | vttbr_base); // VMID 1, base at 0x10000
    // VTCR: TG0=16KB (0b10 << 14), SL0=Level 2 (0b01 << 6), T0SZ=16 (48-bit IPA)
    s2.set_vtcr((0b10ULL << 14u) | (0b01ULL << 6u) | 16ULL);
    s2.set_hcr(0x1ULL); // VM = 1 (Stage 2 enabled)

    const std::uint64_t test_ipa = 0x80000ULL;
    const std::uint64_t test_pa  = 0x140000ULL;

    // Step 1: Arm single-shot fault injector at test_ipa
    Stage2FaultInjector injector;
    Stage2InjectedFault rule{};
    rule.ipa = test_ipa;
    rule.ipa_mask = 0xFFFFFFFFFFFFC000ULL; // 16KB page mask
    rule.fault_type = ::mmu::MmuFaultType::Translation;
    rule.level = 3;
    rule.max_triggers = 1;
    rule.active = true;
    injector.add_rule(rule);
    s2.set_fault_injector(&injector);

    // Step 2: Attempt access -> Fault injected
    Stage2Fault fault{};
    auto trans_res1 = s2.translate(mem, test_ipa, Stage2AccessType::Write, &fault);
    TEST_CHECK(!trans_res1.has_value(), "Fault injector must intercept access and return fault");

    auto syndrome = make_stage2_syndrome_report(fault, 0x10000000ULL + test_ipa);
    // Verify syndrome fields
    TEST_CHECK((syndrome.esr_el2 >> 26u) == 0x24u, "ESR EC must be 0x24 (Data Abort)");
    TEST_CHECK((syndrome.esr_el2 & 0x40u) != 0, "ESR WnR bit must be 1 for write access");
    TEST_CHECK((syndrome.esr_el2 & 0x3Fu) == 0x07u, "FSC must be 0x07 (Translation fault level 3)");
    TEST_CHECK(syndrome.hpfar_el2 == ((test_ipa >> 8u) & 0x000000FFFFFFFFF0ULL), "HPFAR must hold faulting IPA frame");

    // Step 3: Hypervisor remap handler
    const std::uint64_t hpfar_ipa = (syndrome.hpfar_el2 & 0x000000FFFFFFFFF0ULL) << 8u;
    TEST_CHECK(hpfar_ipa == test_ipa, "Hypervisor must recover exact faulting page address from HPFAR");

    // Hypervisor maps the page in memory
    mem.write_u64(test_pa, 0xAABBCCDDEEFF0011ULL);
    bool map_ok = Stage2TranslationEngine::map_page_16k(mem, vttbr_base, hpfar_ipa, test_pa, true, true, true);
    TEST_CHECK(map_ok, "Hypervisor Stage2 mapping must succeed");

    // Step 4: Guest resumes execution (ERET) -> Re-attempt translation
    // Since rule had max_triggers = 1, injector auto-deactivated
    TEST_CHECK(!injector.rules()[0].active, "Single-shot rule must be auto-deactivated");

    // Replay translation
    Stage2Fault replay_fault{};
    auto trans_res2 = s2.translate(mem, test_ipa, Stage2AccessType::Write, &replay_fault);
    TEST_CHECK(trans_res2.has_value(), "Stage-2 translation must succeed on replay");
    TEST_CHECK(trans_res2->physical_address == test_pa, "Translated PA must match mapped PA");

    std::cout << "  -> Stage-2 fault remap & replay lifecycle verified.\n";
}

} // namespace

int main() {
    std::cout << "==================================================================\n";
    std::cout << "Stage 203.2: EL2 Syndrome & HPFAR Architectural Audit Tests\n";
    std::cout << "==================================================================\n";

    test_esr_el2_architectural_oracles();
    test_hpfar_el2_reconstruction();
    test_fault_remap_replay_lifecycle();

    std::cout << "------------------------------------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "ALL STAGE 203.2 TESTS PASSED.\n";
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED.\n";
        return 1;
    }
}
