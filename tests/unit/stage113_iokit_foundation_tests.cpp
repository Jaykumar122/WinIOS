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
#include "runtime/darwin_platform_expert.h"
#include "runtime/iokit_registry.h"

#define IOKIT_TEST_ASSERT(cond, msg) \
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
// Test 1: OSObject Reference Counting
// -----------------------------------------------------------------------------
bool test_osobject_refcounting() {
    std::cout << "[RUN] Test 1: OSObject Reference Counting ... ";

    OSObject obj;
    IOKIT_TEST_ASSERT(obj.retain_count() == 1, "Initial retain count must be 1");

    obj.retain();
    IOKIT_TEST_ASSERT(obj.retain_count() == 2, "Retain must increment count to 2");

    obj.release();
    IOKIT_TEST_ASSERT(obj.retain_count() == 1, "Release must decrement count to 1");

    obj.release();
    IOKIT_TEST_ASSERT(obj.retain_count() == 0, "Release must reach 0");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: IORegistryEntry Properties Management
// -----------------------------------------------------------------------------
bool test_registry_entry_properties() {
    std::cout << "[RUN] Test 2: IORegistryEntry Property Management ... ";

    IORegistryEntry entry("test_node");
    IOKIT_TEST_ASSERT(entry.name() == "test_node", "Name must match initialized string");

    entry.set_property_string("model", "iPhone15,4");
    entry.set_property_u32("clock-frequency", 24000000u);
    entry.set_property_u64("reg-base", 0x08000000ULL);

    IOKIT_TEST_ASSERT(entry.has_property("model"), "Must have property 'model'");
    IOKIT_TEST_ASSERT(entry.get_property_string("model") == "iPhone15,4", "Property 'model' must match");
    IOKIT_TEST_ASSERT(entry.get_property_u32("clock-frequency") == 24000000u, "Frequency must match 24 MHz");
    IOKIT_TEST_ASSERT(entry.get_property_u64("reg-base") == 0x08000000ULL, "Base must match 0x08000000");

    IOKIT_TEST_ASSERT(entry.get_property_u32("non-existent", 999) == 999, "Default value returned for missing property");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Multi-Plane Linkage (gIOServicePlane & gIODTPlane)
// -----------------------------------------------------------------------------
bool test_multi_plane_linkage() {
    std::cout << "[RUN] Test 3: Multi-Plane Linkage & Hierarchy ... ";

    IORegistryEntry root("Root");
    IORegistryEntry armio("arm-io");
    IORegistryEntry aic("aic");
    IORegistryEntry pe("AppleARMPE");

    // In gIODTPlane: Root -> arm-io -> aic
    armio.attach_to_plane(kIODTPlane, &root);
    aic.attach_to_plane(kIODTPlane, &armio);

    // In gIOServicePlane: Root -> AppleARMPE -> aic
    pe.attach_to_plane(kIOServicePlane, &root);
    aic.attach_to_plane(kIOServicePlane, &pe);

    // Verify gIODTPlane path
    IOKIT_TEST_ASSERT(aic.get_path(kIODTPlane) == "/Root/arm-io/aic", "DT plane path must be /Root/arm-io/aic");
    IOKIT_TEST_ASSERT(aic.get_parent(kIODTPlane) == &armio, "Parent on DT plane must be arm-io");

    // Verify gIOServicePlane path
    IOKIT_TEST_ASSERT(aic.get_path(kIOServicePlane) == "/Root/AppleARMPE/aic", "Service plane path must be /Root/AppleARMPE/aic");
    IOKIT_TEST_ASSERT(aic.get_parent(kIOServicePlane) == &pe, "Parent on service plane must be AppleARMPE");

    // Verify children lists
    const auto& dt_children = armio.get_children(kIODTPlane);
    IOKIT_TEST_ASSERT(dt_children.size() == 1 && dt_children[0] == &aic, "arm-io has child aic on DT plane");

    const auto& svc_children = pe.get_children(kIOServicePlane);
    IOKIT_TEST_ASSERT(svc_children.size() == 1 && svc_children[0] == &aic, "AppleARMPE has child aic on service plane");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: IOService Lifecycle State Machine
// -----------------------------------------------------------------------------
bool test_ioservice_lifecycle() {
    std::cout << "[RUN] Test 4: IOService Lifecycle State Machine ... ";

    IOService svc("test_service");
    svc.set_io_class("AppleA16InterruptController");
    svc.set_probe_score(50000);

    IOKIT_TEST_ASSERT(svc.service_state() == IOServiceState::Registered, "Initial state Registered");

    IOKIT_TEST_ASSERT(svc.init(), "init() must succeed");

    std::int32_t score = 0;
    IOKIT_TEST_ASSERT(svc.probe(nullptr, &score) == 50000, "probe() must return probe score");
    IOKIT_TEST_ASSERT(score == 50000, "Score out-param set to 50000");
    IOKIT_TEST_ASSERT(svc.service_state() == IOServiceState::Probed, "State transitions to Probed");

    IOKIT_TEST_ASSERT(svc.start(nullptr), "start() must succeed");
    IOKIT_TEST_ASSERT(svc.service_state() == IOServiceState::Active, "State transitions to Active");

    svc.stop(nullptr);
    IOKIT_TEST_ASSERT(svc.service_state() == IOServiceState::Terminated, "State transitions to Terminated");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: IOPlatformDevice Hardware Attributes
// -----------------------------------------------------------------------------
bool test_platform_device_attributes() {
    std::cout << "[RUN] Test 5: IOPlatformDevice Hardware Attributes ... ";

    IOPlatformDevice dev("uart0");
    dev.set_mmio(0x09000000ULL, 0x1000ULL);
    dev.set_irq_line(33);
    dev.set_compatible_tokens({"apple,uart-1", "arm,pl011"});

    IOKIT_TEST_ASSERT(dev.mmio_base() == 0x09000000ULL, "MMIO base must be 0x09000000");
    IOKIT_TEST_ASSERT(dev.mmio_size() == 0x1000ULL, "MMIO size must be 4 KB");
    IOKIT_TEST_ASSERT(dev.irq_line() == 33, "IRQ line must be 33");

    const auto& tokens = dev.compatible_tokens();
    IOKIT_TEST_ASSERT(tokens.size() == 2, "Must contain 2 compatible tokens");
    IOKIT_TEST_ASSERT(tokens[0] == "apple,uart-1", "First token is apple,uart-1");
    IOKIT_TEST_ASSERT(tokens[1] == "arm,pl011", "Second token is arm,pl011");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: IORegistry Construction from iPhone 15 DeviceTree
// -----------------------------------------------------------------------------
bool test_registry_construction_from_dtb() {
    std::cout << "[RUN] Test 6: IORegistry Construction from DeviceTree ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    std::string err;
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size(), &err);
    IOKIT_TEST_ASSERT(dt_root != nullptr, "DTB root must be deserialized");

    IORegistryManager mgr;
    IOKIT_TEST_ASSERT(mgr.build_from_devicetree(*dt_root), "Registry build from DTB must succeed");

    // Verify presence of nodes in gIODTPlane
    const std::size_t dt_count = mgr.count_entries_in_plane(kIODTPlane);
    IOKIT_TEST_ASSERT(dt_count > 10, "Registry must contain multiple nodes on gIODTPlane");

    const std::size_t svc_count = mgr.count_entries_in_plane(kIOServicePlane);
    IOKIT_TEST_ASSERT(svc_count > 10, "Registry must contain multiple nodes on gIOServicePlane");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Plane Path Traversal & Device Lookup
// -----------------------------------------------------------------------------
bool test_plane_path_lookup() {
    std::cout << "[RUN] Test 7: Plane Path Traversal & Device Lookup ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    IORegistryManager mgr;
    mgr.build_from_devicetree(*dt_root);

    // Look up AIC node on gIODTPlane: /Root/device-tree/arm-io/aic
    auto* aic = mgr.lookup_by_path(kIODTPlane, "device-tree/arm-io/aic");
    IOKIT_TEST_ASSERT(aic != nullptr, "AIC entry must be found in gIODTPlane");
    IOKIT_TEST_ASSERT(aic->name() == "aic", "Entry name must be aic");

    // Look up PMGR node on gIODTPlane
    auto* pmgr = mgr.lookup_by_path(kIODTPlane, "device-tree/arm-io/pmgr");
    IOKIT_TEST_ASSERT(pmgr != nullptr, "PMGR entry must be found in gIODTPlane");
    IOKIT_TEST_ASSERT(pmgr->name() == "pmgr", "Entry name must be pmgr");

    // Look up CPU0 node on gIODTPlane
    auto* cpu0 = mgr.lookup_by_path(kIODTPlane, "device-tree/cpus/cpu0");
    IOKIT_TEST_ASSERT(cpu0 != nullptr, "cpu0 entry must be found in gIODTPlane");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Driver Personality Probe Scoring & Arbitration
// -----------------------------------------------------------------------------
bool test_personality_probe_scoring() {
    std::cout << "[RUN] Test 8: Personality Matching & Probe Score Ranking ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    const auto& catalog = pe.catalog();
    IOKIT_TEST_ASSERT(!catalog.empty(), "Catalog must not be empty");

    // Find personalities and verify ranking
    const IOKitPersonality* pe_root = nullptr;
    const IOKitPersonality* pe_aic = nullptr;
    const IOKitPersonality* pe_uart = nullptr;

    for (const auto& p : catalog) {
        if (p.io_class == "AppleARMPE") pe_root = &p;
        if (p.io_class == "AppleA16InterruptController") pe_aic = &p;
        if (p.io_class == "AppleSerialUART") pe_uart = &p;
    }

    IOKIT_TEST_ASSERT(pe_root != nullptr, "AppleARMPE personality must exist");
    IOKIT_TEST_ASSERT(pe_aic != nullptr, "AppleA16InterruptController personality must exist");
    IOKIT_TEST_ASSERT(pe_uart != nullptr, "AppleSerialUART personality must exist");

    IOKIT_TEST_ASSERT(pe_root->probe_score == 100000, "AppleARMPE score must be 100000");
    IOKIT_TEST_ASSERT(pe_aic->probe_score == 50000, "AppleA16InterruptController score must be 50000");
    IOKIT_TEST_ASSERT(pe_uart->probe_score == 40000, "AppleSerialUART score must be 40000");

    IOKIT_TEST_ASSERT(pe_root->probe_score > pe_aic->probe_score, "Root PE score higher than AIC");
    IOKIT_TEST_ASSERT(pe_aic->probe_score > pe_uart->probe_score, "AIC score higher than UART");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Power Plane (gIOPowerPlane) Linkage
// -----------------------------------------------------------------------------
bool test_power_plane_linkage() {
    std::cout << "[RUN] Test 9: gIOPowerPlane Power Domain Structure ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    IORegistryManager mgr;
    mgr.build_from_devicetree(*dt_root);

    // Verify power plane root exists
    auto* power_root = mgr.get_plane_root(kIOPowerPlane);
    IOKIT_TEST_ASSERT(power_root != nullptr, "gIOPowerPlane root must exist");
    IOKIT_TEST_ASSERT(power_root->name() == "IOPowerRoot", "Power plane root name is IOPowerRoot");

    // Verify PMGR is attached to gIOPowerPlane
    auto* pmgr = mgr.lookup_by_path(kIODTPlane, "device-tree/arm-io/pmgr");
    IOKIT_TEST_ASSERT(pmgr != nullptr, "PMGR must exist");
    IOKIT_TEST_ASSERT(pmgr->in_plane(kIOPowerPlane), "PMGR must participate in gIOPowerPlane");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Complete Platform Expert Readiness Verification
// -----------------------------------------------------------------------------
bool test_platform_expert_readiness_verification() {
    std::cout << "[RUN] Test 10: Platform Expert Complete Readiness Verification ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    auto dt_root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size());
    std::string err;
    IOKIT_TEST_ASSERT(pe.probe_and_match(*dt_root, err), "probe_and_match must succeed");

    std::string diag;
    IOKIT_TEST_ASSERT(pe.verify_platform_readiness(diag), "Platform readiness verification must succeed");
    IOKIT_TEST_ASSERT(diag.find("Platform Expert: AppleARMPE active") != std::string::npos, "Diagnostic must confirm Root PE");
    IOKIT_TEST_ASSERT(diag.find("AppleA16InterruptController active") != std::string::npos, "Diagnostic must confirm AIC2");
    IOKIT_TEST_ASSERT(diag.find("AppleT8120PMGR active") != std::string::npos, "Diagnostic must confirm PMGR");
    IOKIT_TEST_ASSERT(diag.find("AppleDCP active") != std::string::npos, "Diagnostic must confirm Display");
    IOKIT_TEST_ASSERT(diag.find("AppleSerialUART active") != std::string::npos, "Diagnostic must confirm UART");
    IOKIT_TEST_ASSERT(diag.find("6 cores verified") != std::string::npos, "Diagnostic must confirm 6 CPU cores");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "=================================================================\n" << std::flush;
        std::cout << " Stage 113: IOKit Foundation & Registry Construction Suite     \n" << std::flush;
        std::cout << "=================================================================\n" << std::flush;

        int passed = 0;
        int total = 10;

        if (test_osobject_refcounting()) ++passed; std::cout << std::flush;
        if (test_registry_entry_properties()) ++passed; std::cout << std::flush;
        if (test_multi_plane_linkage()) ++passed; std::cout << std::flush;
        if (test_ioservice_lifecycle()) ++passed; std::cout << std::flush;
        if (test_platform_device_attributes()) ++passed; std::cout << std::flush;
        if (test_registry_construction_from_dtb()) ++passed; std::cout << std::flush;
        if (test_plane_path_lookup()) ++passed; std::cout << std::flush;
        if (test_personality_probe_scoring()) ++passed; std::cout << std::flush;
        if (test_power_plane_linkage()) ++passed; std::cout << std::flush;
        if (test_platform_expert_readiness_verification()) ++passed; std::cout << std::flush;

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
