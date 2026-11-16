#include <iostream>
#include <cstdint>

#include "device/apple_aic.h"

using namespace winios::device;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_aic2_conformance() {
    std::cout << "[Test 1] Stage 208.15: Apple AIC2 Hardware Model Conformance...\n";

    // Create AIC2 instance at 0x23B100000 with 6 cores
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL);
    aic.set_core_count(6);

    // 1. Check AIC_INFO (Offset 4)
    // Bits [31:24] = 6 cores, [23:16] = 1 die, [15:0] = 512 IRQs -> 0x06010200
    std::uint64_t val = aic.read_register(0x4, 4);
    std::cout << "  -> AIC_INFO (offset 0x4): 0x" << std::hex << val << std::dec << "\n";
    TEST_CHECK(val == 0x06010200ULL, "AIC_INFO matches 6 cores, 1 die, 512 IRQs (0x06010200)");

    // 2. Check AIC_WHOAMI (Offset 0x2000)
    val = aic.read_register(0x2000, 4);
    std::cout << "  -> AIC_WHOAMI (offset 0x2000): 0x" << std::hex << val << std::dec << "\n";
    TEST_CHECK(val == 0x0ULL, "AIC_WHOAMI is Core 0 on primary core");

    // 3. Write AIC_CONFIG (Offset 0x10)
    aic.write_register(0x10, 1ULL, 4);
    val = aic.read_register(0x10, 4);
    TEST_CHECK(val == 1ULL, "AIC_CONFIG enabled (value 1)");

    // 4. Assert and query interrupt signal
    aic.set_hw_irq(32, true);
    TEST_CHECK(aic.is_hw_irq_asserted(32), "IRQ 32 asserted");
    aic.set_hw_irq(32, false);
    TEST_CHECK(!aic.is_hw_irq_asserted(32), "IRQ 32 cleared");

    // 5. Test State Serialization & Deserialization
    const auto snap = aic.serialize_state();
    TEST_CHECK(!snap.empty(), "Serialized AIC2 state");

    AppleAIC aic2(0x41494332u, 0x23B100000ULL, 0x100000ULL);
    TEST_CHECK(aic2.deserialize_state(snap.data(), snap.size()), "Restored AIC2 from snapshot");
    TEST_CHECK(aic2.read_register(0x4, 4) == 0x06010200ULL, "Restored AIC2 retains AIC_INFO");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.15 Apple AIC2 Conformance Tests ===\n";
    test_aic2_conformance();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.15: All AIC2 conformance tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.15: " << g_failures << " tests failed.\n";
        return 1;
    }
}
