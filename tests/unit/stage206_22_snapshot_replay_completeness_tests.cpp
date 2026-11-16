#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "device/apple_aic.h"
#include "snapshot/smp_snapshot_manager.h"

using namespace winios;
using namespace winios::device;
using namespace winios::snapshot;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_aic_and_system_state_serialization() {
    std::cout << "[Test 1] Apple AIC2 & System State Snapshot Serialization...\n";

    // 1. Configure and dirty AIC state
    AppleAIC aic1(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    aic1.write_register(AppleAIC::kRegConfig, 0u, 4); // Disable AIC
    aic1.write_register(AppleAIC::kRegIpiSend, 0x12u, 4); // Send IPI to Cores 1 and 4

    TEST_CHECK(aic1.read_register(AppleAIC::kRegConfig, 4) == 0u, "AIC1 is disabled");
    TEST_CHECK(aic1.get_pending_ipi(1) != 0, "Core 1 has pending IPI");
    TEST_CHECK(aic1.get_pending_ipi(4) != 0, "Core 4 has pending IPI");

    // 2. Serialize AIC state
    const auto blob = aic1.serialize_state();
    TEST_CHECK(!blob.empty(), "Serialized blob must not be empty");
    TEST_CHECK(blob.size() >= 64, "Blob contains minimum header and registers");

    // 3. Restore into freshly instantiated AIC
    AppleAIC aic2(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    TEST_CHECK(aic2.read_register(AppleAIC::kRegConfig, 4) == 1u, "AIC2 initially enabled on reset");
    TEST_CHECK(aic2.get_pending_ipi(1) == 0, "AIC2 initially has no pending IPI on Core 1");

    bool rest_ok = aic2.deserialize_state(blob.data(), blob.size());
    TEST_CHECK(rest_ok, "Deserialization must succeed");

    // 4. Verify bit-exact restoration
    TEST_CHECK(aic2.read_register(AppleAIC::kRegConfig, 4) == 0u, "Restored AIC2 is disabled");
    TEST_CHECK(aic2.get_pending_ipi(1) != 0, "Restored Core 1 has pending IPI");
    TEST_CHECK(aic2.get_pending_ipi(4) != 0, "Restored Core 4 has pending IPI");
    TEST_CHECK(aic2.get_pending_ipi(0) == 0, "Restored Core 0 has no pending IPI");

    std::cout << "  -> Snapshot serialization and restoration verified.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.22 Snapshot & Replay Completeness Tests ===\n";
    test_aic_and_system_state_serialization();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.22 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.22 Tests FAILED ===\n";
        return 1;
    }
}
