#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "device/apple_devicetree.h"
#include "device/apple_pmgr.h"
#include "runtime/darwin_platform_expert.h"
#include "runtime/iokit_registry.h"

#define PMGR_TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::device;
using namespace winios::runtime;

namespace {

// -----------------------------------------------------------------------------
// Test 1: AppleARMPE Root Driver Matching & System Identity
// -----------------------------------------------------------------------------
bool test_apple_arm_pe_matching() {
    std::cout << "[RUN] Test 1: AppleARMPE Platform Expert Matching & Identity ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    std::string err;
    PMGR_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "Driver matching must succeed");

    const auto* root_driver = pe.find_driver_by_path("/");
    PMGR_TEST_ASSERT(root_driver != nullptr, "Root driver must be matched");
    PMGR_TEST_ASSERT(root_driver->personality.io_class == "AppleARMPE", "Root driver must be AppleARMPE");
    PMGR_TEST_ASSERT(root_driver->final_score >= 100000, "AppleARMPE must achieve top probe score >= 100000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: PMGR Chip Identification & Silicon Revision
// -----------------------------------------------------------------------------
bool test_pmgr_chip_id_and_revision() {
    std::cout << "[RUN] Test 2: PMGR Chip ID & Silicon Revision Conformance ... ";

    ApplePMGR pmgr(0x504D4752u, ApplePMGR::kDefaultPmgrBase, ApplePMGR::kDefaultPmgrSize, ApplePMGR::kChipIdT8120);

    PMGR_TEST_ASSERT(pmgr.get_chip_id() == 0x8120u, "Chip ID must be 0x8120 for A16 Bionic");

    // Read Chip ID register (0x0000)
    const std::uint64_t chip_id_reg = pmgr.read_register(ApplePMGR::kRegChipId, 4);
    PMGR_TEST_ASSERT(chip_id_reg == 0x8120ULL, "Chip ID register read must yield 0x8120");

    // Read Revision register (0x0004)
    const std::uint64_t rev_reg = pmgr.read_register(ApplePMGR::kRegRevision, 4);
    PMGR_TEST_ASSERT(rev_reg == 0x0001ULL, "Silicon Revision register must return 0x0001 (Rev A0)");

    // Read System Power Status (0x0008)
    const std::uint64_t status_reg = pmgr.read_register(ApplePMGR::kRegSysStatus, 4);
    PMGR_TEST_ASSERT(status_reg == 1ULL, "System status must report 1 (Running)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Power Domain Topology & Initial Power States
// -----------------------------------------------------------------------------
bool test_pmgr_power_domain_topology() {
    std::cout << "[RUN] Test 3: Power Domain Topology & Initial Power States ... ";

    ApplePMGR pmgr;

    // Verify all core domains are initially active / full power
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster0), "Everest cluster domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster1), "Sawtooth cluster domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainAic), "AIC2 domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainUart0), "UART0 domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainDisplay), "Display domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainGpu), "GPU domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainAne), "ANE domain must be powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainMedia), "Media domain must be powered");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Clock Generation, PLL Lock Bit & Hardware Handshake
// -----------------------------------------------------------------------------
bool test_clock_generation_and_pll_locking() {
    std::cout << "[RUN] Test 4: Clock Generation, PLL Lock & Handshake ... ";

    ApplePMGR pmgr;

    // Read power gate register for GPU domain (0x0300)
    // Register format:
    // bits [3:0] = target state
    // bits [7:4] = current actual state
    // bit [8]    = PLL lock / clock stable flag (1 if powered, 0 if off)
    std::uint64_t reg_val = pmgr.read_register(ApplePMGR::kDomainGpu, 4);
    PMGR_TEST_ASSERT((reg_val & 0x100u) != 0, "PLL lock bit [8] must be 1 when domain is powered");

    // Power down GPU domain by writing 0x0
    pmgr.write_register(ApplePMGR::kDomainGpu, 0x0u, 4);

    reg_val = pmgr.read_register(ApplePMGR::kDomainGpu, 4);
    PMGR_TEST_ASSERT((reg_val & 0x0Fu) == 0, "Target state should be 0 (Off)");
    PMGR_TEST_ASSERT((reg_val & 0xF0u) == 0, "Actual state should be 0 (Off)");
    PMGR_TEST_ASSERT((reg_val & 0x100u) == 0, "PLL lock bit [8] must be 0 when domain is unpowered");
    PMGR_TEST_ASSERT(!pmgr.is_domain_powered(ApplePMGR::kDomainGpu), "Domain is_powered must return false");

    // Re-power GPU domain by writing 0xF (Full)
    pmgr.write_register(ApplePMGR::kDomainGpu, ApplePMGR::kPowerStateFull, 4);

    reg_val = pmgr.read_register(ApplePMGR::kDomainGpu, 4);
    PMGR_TEST_ASSERT((reg_val & 0x0Fu) == 0xFu, "Target state must be 0xF");
    PMGR_TEST_ASSERT((reg_val & 0xF0u) == 0xF0u, "Actual state must be 0xF");
    PMGR_TEST_ASSERT((reg_val & 0x100u) != 0, "PLL lock bit [8] must reassert upon power-on");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainGpu), "Domain is_powered must return true");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Dynamic Power Gating State Transitions
// -----------------------------------------------------------------------------
bool test_power_gating_state_transitions() {
    std::cout << "[RUN] Test 5: Power Gating State Machine Transitions ... ";

    ApplePMGR pmgr;

    // Transition ANE through power states: Off -> Low -> Active -> Full
    const std::uint64_t ane_off = ApplePMGR::kDomainAne;

    pmgr.set_domain_power_state(ane_off, ApplePMGR::kPowerStateOff);
    PMGR_TEST_ASSERT(pmgr.get_domain_power_state(ane_off) == ApplePMGR::kPowerStateOff, "State is Off (0)");

    pmgr.set_domain_power_state(ane_off, ApplePMGR::kPowerStateLow);
    PMGR_TEST_ASSERT(pmgr.get_domain_power_state(ane_off) == ApplePMGR::kPowerStateLow, "State is Low (1)");

    pmgr.set_domain_power_state(ane_off, ApplePMGR::kPowerStateActive);
    PMGR_TEST_ASSERT(pmgr.get_domain_power_state(ane_off) == ApplePMGR::kPowerStateActive, "State is Active (4)");

    pmgr.set_domain_power_state(ane_off, ApplePMGR::kPowerStateFull);
    PMGR_TEST_ASSERT(pmgr.get_domain_power_state(ane_off) == ApplePMGR::kPowerStateFull, "State is Full (15)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: System Reset Handshake & Triggering
// -----------------------------------------------------------------------------
bool test_system_reset_trigger() {
    std::cout << "[RUN] Test 6: System Reset Handshake Triggering ... ";

    ApplePMGR pmgr;
    PMGR_TEST_ASSERT(!pmgr.was_reset_requested(), "Reset flag must initially be false");

    // Write to reset trigger register (0x0010) with reboot command
    pmgr.write_register(ApplePMGR::kRegResetTrigger, 0xDEADBEEFULL, 4);
    PMGR_TEST_ASSERT(pmgr.was_reset_requested(), "Reset request flag must be asserted after write to 0x0010");

    // Reading reset register reflects pending reset
    const std::uint64_t val = pmgr.read_register(ApplePMGR::kRegResetTrigger, 4);
    PMGR_TEST_ASSERT(val == 1ULL, "Read of reset trigger register should return 1");

    // Clear reset request
    pmgr.clear_reset_request();
    PMGR_TEST_ASSERT(!pmgr.was_reset_requested(), "Reset flag must be false after clear");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: DeviceTree PMGR Node Conformance & Driver Binding
// -----------------------------------------------------------------------------
bool test_pmgr_devicetree_binding() {
    std::cout << "[RUN] Test 7: DeviceTree PMGR Node Conformance ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    PMGR_TEST_ASSERT(dt_root != nullptr, "DTB root must exist");

    const auto* armio = dt_root->find_child("arm-io");
    PMGR_TEST_ASSERT(armio != nullptr, "/arm-io must exist");

    const auto* pmgr_node = armio->find_child("pmgr");
    PMGR_TEST_ASSERT(pmgr_node != nullptr, "/arm-io/pmgr node must exist");

    // Compatible string
    const auto* comp = pmgr_node->find_property("compatible");
    PMGR_TEST_ASSERT(comp != nullptr, "PMGR must have compatible property");
    const char* comp_str = reinterpret_cast<const char*>(comp->data.data());
    PMGR_TEST_ASSERT(std::string(comp_str) == "apple,t8120-pmgr", "Primary compatible must be apple,t8120-pmgr");

    // MMIO register aperture
    const auto* reg = pmgr_node->find_property("reg");
    PMGR_TEST_ASSERT(reg != nullptr && reg->data.size() >= 16, "PMGR reg property must be valid");
    std::uint64_t reg_vals[2] = {0, 0};
    std::memcpy(reg_vals, reg->data.data(), sizeof(reg_vals));
    PMGR_TEST_ASSERT(reg_vals[0] == 0x10000000ULL, "PMGR MMIO base must be 0x10000000");
    PMGR_TEST_ASSERT(reg_vals[1] == 0x00100000ULL, "PMGR MMIO size must be 1 MB");

    // Platform expert matching
    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();
    std::string err;
    PMGR_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "Driver matching must succeed");

    const auto* pmgr_driver = pe.find_driver_by_class("AppleT8120PMGR");
    PMGR_TEST_ASSERT(pmgr_driver != nullptr, "AppleT8120PMGR driver must be bound");
    PMGR_TEST_ASSERT(pmgr_driver->mmio_base == 0x10000000ULL, "Driver base must match 0x10000000");
    PMGR_TEST_ASSERT(pmgr_driver->personality.probe_score == 50000, "PMGR probe score must be 50000");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: PMGR State Serialization & Snapshot Round-Trip
// -----------------------------------------------------------------------------
bool test_pmgr_snapshot_serialization() {
    std::cout << "[RUN] Test 8: PMGR State Serialization & Snapshot Restore ... ";

    ApplePMGR pmgr1;
    pmgr1.set_domain_power_state(ApplePMGR::kDomainGpu, ApplePMGR::kPowerStateOff);
    pmgr1.set_domain_power_state(ApplePMGR::kDomainAne, ApplePMGR::kPowerStateLow);
    pmgr1.write_register(ApplePMGR::kRegResetTrigger, 1, 4);

    const auto state = pmgr1.serialize_state();
    PMGR_TEST_ASSERT(!state.empty(), "Serialized PMGR state must not be empty");

    ApplePMGR pmgr2;
    PMGR_TEST_ASSERT(pmgr2.deserialize_state(state.data(), state.size()), "Deserialization must succeed");

    PMGR_TEST_ASSERT(pmgr2.get_chip_id() == 0x8120u, "Restored chip ID must match");
    PMGR_TEST_ASSERT(pmgr2.get_domain_power_state(ApplePMGR::kDomainGpu) == ApplePMGR::kPowerStateOff, "Restored GPU must be Off");
    PMGR_TEST_ASSERT(pmgr2.get_domain_power_state(ApplePMGR::kDomainAne) == ApplePMGR::kPowerStateLow, "Restored ANE must be Low");
    PMGR_TEST_ASSERT(pmgr2.was_reset_requested(), "Restored reset flag must be asserted");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Platform Initialization Order Conformance
// -----------------------------------------------------------------------------
bool test_platform_init_order_conformance() {
    std::cout << "[RUN] Test 9: Platform Initialization Order & Dependencies ... ";

    // 1. DeviceTree parsed
    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    PMGR_TEST_ASSERT(dt_root != nullptr, "Step 1: DTB available");

    // 2. Platform Expert instantiates
    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();
    std::string err;
    PMGR_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "Step 2: PE driver match");

    // 3. PMGR power domains verified stable
    ApplePMGR pmgr;
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster0), "Step 3: CPU Everest cluster powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainCpuCluster1), "Step 3: CPU Sawtooth cluster powered");
    PMGR_TEST_ASSERT(pmgr.is_domain_powered(ApplePMGR::kDomainAic), "Step 3: AIC2 powered");

    // 4. Registry constructed
    IORegistryManager mgr;
    PMGR_TEST_ASSERT(mgr.build_from_devicetree(*dt_root), "Step 4: IORegistry constructed");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Complete Platform Expert Readiness Verification
// -----------------------------------------------------------------------------
bool test_platform_expert_full_readiness() {
    std::cout << "[RUN] Test 10: Platform Expert Complete Peripheral Audit ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    std::string err;
    PMGR_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "probe_and_match must succeed");

    std::string diag;
    PMGR_TEST_ASSERT(pe.verify_platform_readiness(diag), "Platform readiness verification must succeed");
    PMGR_TEST_ASSERT(diag.find("AppleARMPE active") != std::string::npos, "Platform Expert root active");
    PMGR_TEST_ASSERT(diag.find("AppleT8120PMGR active") != std::string::npos, "Power manager active");
    PMGR_TEST_ASSERT(diag.find("AppleA16InterruptController active") != std::string::npos, "Interrupt controller active");
    PMGR_TEST_ASSERT(diag.find("AppleDCP active") != std::string::npos, "Display controller active");
    PMGR_TEST_ASSERT(diag.find("AppleSerialUART active") != std::string::npos, "UART console active");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "=================================================================\n" << std::flush;
        std::cout << " Stage 114: Platform Expert & PMGR Conformance Suite            \n" << std::flush;
        std::cout << "=================================================================\n" << std::flush;

        int passed = 0;
        int total = 10;

        if (test_apple_arm_pe_matching()) ++passed; std::cout << std::flush;
        if (test_pmgr_chip_id_and_revision()) ++passed; std::cout << std::flush;
        if (test_pmgr_power_domain_topology()) ++passed; std::cout << std::flush;
        if (test_clock_generation_and_pll_locking()) ++passed; std::cout << std::flush;
        if (test_power_gating_state_transitions()) ++passed; std::cout << std::flush;
        if (test_system_reset_trigger()) ++passed; std::cout << std::flush;
        if (test_pmgr_devicetree_binding()) ++passed; std::cout << std::flush;
        if (test_pmgr_snapshot_serialization()) ++passed; std::cout << std::flush;
        if (test_platform_init_order_conformance()) ++passed; std::cout << std::flush;
        if (test_platform_expert_full_readiness()) ++passed; std::cout << std::flush;

        std::cout << "=================================================================\n";
        std::cout << " Summary: " << passed << " / " << total << " tests passed.\n";
        std::cout << "=================================================================\n" << std::flush;

        return (passed == total) ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "UNHANDLED EXCEPTION in main(): " << e.what() << std::endl;
        return 2;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in main()" << std::endl;
        return 3;
    }
}
