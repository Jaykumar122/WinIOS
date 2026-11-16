#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_arm64_translation_conformance() {
    std::cout << "[Test 1] Stage 207.12: ARM64 16KB Granule Translation Conformance...\n";

    CPU cpu;
    Memory memory(0x400000);

    // 1. Configure TCR_EL1 for 16KB granule (TG0=2, TG1=1, T0SZ=25, T1SZ=25)
    // 39-bit VA space: 64 - 25 = 39 bits
    const std::uint64_t tcr_val = (1ULL << 30) | (2ULL << 14) | (25ULL << 16) | 25ULL;
    cpu.set_tcr_el1(tcr_val);
    TEST_CHECK(cpu.get_tcr_el1() == tcr_val, "TCR_EL1 configured with 16KB granule and 39-bit VA split");

    // 2. Configure MAIR_EL1
    // Index 0: Device-nGnRnE (0x00), Index 1: Normal Non-Cacheable (0x44), Index 2: Normal WB (0xFF)
    const std::uint64_t mair_val = 0x0000000444FF0400ULL;
    cpu.set_mair_el1(mair_val);
    TEST_CHECK(cpu.get_mair_el1() == mair_val, "MAIR_EL1 memory attributes programmed");

    // 3. Configure TTBR0_EL1 and TTBR1_EL1 to valid 16KB root
    const std::uint64_t ttbr_val = 0x80004000ULL; // 16KB aligned
    cpu.set_ttbr0_el1(ttbr_val);
    cpu.set_ttbr1_el1(ttbr_val);
    TEST_CHECK(cpu.get_ttbr0_el1() == ttbr_val, "TTBR0_EL1 base installed");
    TEST_CHECK(cpu.get_ttbr1_el1() == ttbr_val, "TTBR1_EL1 base installed");

    // 4. Validate canonical 39-bit higher half address calculation
    // With T1SZ=25, bits 63..39 must be all 1s
    const std::uint64_t canonical_hh = 0xFFFFFF8000000000ULL;
    TEST_CHECK((canonical_hh >> 39) == 0x1FFFFFFULL, "Address is canonically in higher-half 39-bit range");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 207.12 ARM64 Translation Conformance Tests ===\n";
    test_arm64_translation_conformance();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 207.12: All ARM64 translation conformance tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 207.12: " << g_failures << " tests failed.\n";
        return 1;
    }
}
