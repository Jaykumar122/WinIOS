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

#include "cpu/cpu.h"
#include "cpu/psci.h"
#include "cpu/smp_cluster.h"
#include "device/apple_devicetree.h"
#include "device/apple_hardware_profile.h"
#include "device/apple_pmgr.h"
#include "memory/memory.h"
#include "runtime/darwin_platform_expert.h"

#define SMP_TEST_ASSERT(cond, msg) \
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
// Test 1: 6-Core A16 Bionic Cluster Instantiation & Initial Power States
// -----------------------------------------------------------------------------
bool test_a16_cluster_instantiation() {
    std::cout << "[RUN] Test 1: 6-Core A16 Bionic Cluster & Initial States ... ";

    SMPCluster cluster(6, 0x100000); // 6 cores, 1 MB RAM
    cluster.enable_psci();

    SMP_TEST_ASSERT(cluster.core_count() == 6, "Cluster must have 6 cores for A16 Bionic");

    auto* psci = cluster.get_psci_service();
    SMP_TEST_ASSERT(psci != nullptr, "PSCI service must be active");

    // Core 0 is boot core (On); Secondary cores 1..5 must be Off
    SMP_TEST_ASSERT(psci->get_core_state(0) == CorePowerState::On, "Core 0 must be initially ON");
    for (std::size_t c = 1; c < 6; ++c) {
        SMP_TEST_ASSERT(psci->get_core_state(c) == CorePowerState::Off, "Secondary core must be initially OFF");
        SMP_TEST_ASSERT(cluster.is_core_halted(c), "Secondary core must be halted in cluster");
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: PSCI Version & Features Discovery
// -----------------------------------------------------------------------------
bool test_psci_version_and_features() {
    std::cout << "[RUN] Test 2: PSCI v1.1 Discovery & Supported Features ... ";

    SMPCluster cluster(6, 0x100000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    CPU& cpu0 = cluster.get_core(0);

    // Call PSCI_VERSION (0x84000000)
    cpu0.write_register(0, PSCIService::kVersion);
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "PSCI_VERSION call must be handled");
    SMP_TEST_ASSERT(cpu0.read_register(0) == PSCIService::kPsciVersion, "PSCI version must be 1.1 (0x00010001)");

    // Call PSCI_FEATURES for CPU_ON64 (0xC4000003)
    cpu0.write_register(0, PSCIService::kPsciFeatures);
    cpu0.write_register(1, PSCIService::kCpuOn64);
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "PSCI_FEATURES call must be handled");
    SMP_TEST_ASSERT(static_cast<std::int64_t>(cpu0.read_register(0)) == PSCIService::kSuccess, "CPU_ON64 feature must be supported");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Secondary Core Startup via CPU_ON64 Protocol
// -----------------------------------------------------------------------------
bool test_secondary_core_startup() {
    std::cout << "[RUN] Test 3: Secondary Core Boot Protocol (CPU_ON64) ... ";

    SMPCluster cluster(6, 0x100000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    CPU& cpu0 = cluster.get_core(0);

    // Write NOPs at secondary entry point
    constexpr std::uint64_t kSecEntry = 0x20000ULL;
    cluster.get_memory().write_u32(kSecEntry, 0xD503201Fu); // NOP
    cluster.get_memory().write_u32(kSecEntry + 4, 0xD503201Fu); // NOP

    // Boot Core 1 (Everest Performance Core) with Context ID 0xCAFE1234
    cpu0.write_register(0, PSCIService::kCpuOn64);
    cpu0.write_register(1, 1ULL); // Target Core 1
    cpu0.write_register(2, kSecEntry);
    cpu0.write_register(3, 0xCAFE1234ULL); // Context ID

    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "CPU_ON64 SMC must be handled");
    SMP_TEST_ASSERT(static_cast<std::int64_t>(cpu0.read_register(0)) == PSCIService::kSuccess, "CPU_ON64 must return SUCCESS");

    // Verify Core 1 power state and initial registers
    SMP_TEST_ASSERT(psci->get_core_state(1) == CorePowerState::On, "Core 1 must be ON");
    SMP_TEST_ASSERT(!cluster.is_core_halted(1), "Core 1 must be unhalted");

    CPU& cpu1 = cluster.get_core(1);
    SMP_TEST_ASSERT(cpu1.get_program_counter() == kSecEntry, "Core 1 PC must match requested entry point");
    SMP_TEST_ASSERT(cpu1.read_register(0) == 0xCAFE1234ULL, "Core 1 X0 must receive context ID");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: AFFINITY_INFO64 Core Power State Query
// -----------------------------------------------------------------------------
bool test_affinity_info_queries() {
    std::cout << "[RUN] Test 4: AFFINITY_INFO64 Power State Diagnostics ... ";

    SMPCluster cluster(6, 0x100000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    CPU& cpu0 = cluster.get_core(0);

    // Query Core 0 (should be ON = 0)
    cpu0.write_register(0, PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 0ULL); // Core 0
    cpu0.write_register(2, 0ULL); // Lowest affinity level
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "AFFINITY_INFO64 must succeed");
    SMP_TEST_ASSERT(cpu0.read_register(0) == 0ULL, "Core 0 must report ON (0)");

    // Query Core 2 (should be OFF = 1)
    cpu0.write_register(0, PSCIService::kAffinityInfo64);
    cpu0.write_register(1, 2ULL); // Core 2
    cpu0.write_register(2, 0ULL);
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "AFFINITY_INFO64 must succeed");
    SMP_TEST_ASSERT(cpu0.read_register(0) == 1ULL, "Core 2 must report OFF (1)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Re-boot Rejection (ALREADY_ON) & Invalid Parameter Checks
// -----------------------------------------------------------------------------
bool test_psci_error_conformance() {
    std::cout << "[RUN] Test 5: PSCI Error Conformance (ALREADY_ON & INVALID) ... ";

    SMPCluster cluster(6, 0x100000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    CPU& cpu0 = cluster.get_core(0);

    // Booting Core 0 (already running) must return ALREADY_ON (-4)
    cpu0.write_register(0, PSCIService::kCpuOn64);
    cpu0.write_register(1, 0ULL); // Core 0
    cpu0.write_register(2, 0x10000ULL);
    cpu0.write_register(3, 0ULL);
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "SMC must be handled");
    SMP_TEST_ASSERT(static_cast<std::int64_t>(cpu0.read_register(0)) == PSCIService::kAlreadyOn, "Targeting Core 0 must return ALREADY_ON (-4)");

    // Booting invalid Core 99 must return INVALID_PARAMS (-2)
    cpu0.write_register(0, PSCIService::kCpuOn64);
    cpu0.write_register(1, 99ULL);
    cpu0.write_register(2, 0x10000ULL);
    cpu0.write_register(3, 0ULL);
    SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "SMC must be handled");
    SMP_TEST_ASSERT(static_cast<std::int64_t>(cpu0.read_register(0)) == PSCIService::kInvalidParams, "Invalid core must return INVALID_PARAMS (-2)");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Full 6-Core Heterogeneous Startup (2 Everest + 4 Sawtooth)
// -----------------------------------------------------------------------------
bool test_full_heterogeneous_6core_boot() {
    std::cout << "[RUN] Test 6: Full 6-Core Boot (2 Everest + 4 Sawtooth) ... ";

    SMPCluster cluster(6, 0x100000);
    cluster.enable_psci();
    auto* psci = cluster.get_psci_service();

    // Prepare branch-to-self code for all cores at 0x4000: B . (0x14000000u)
    cluster.get_memory().write_u32(0x4000ULL, 0x14000000u);

    CPU& cpu0 = cluster.get_core(0);

    // Sequentially boot cores 1 through 5
    for (std::uint64_t c = 1; c < 6; ++c) {
        cpu0.write_register(0, PSCIService::kCpuOn64);
        cpu0.write_register(1, c);
        cpu0.write_register(2, 0x4000ULL);
        cpu0.write_register(3, 0x1000ULL + c);

        SMP_TEST_ASSERT(psci->handle_smc(cpu0, 0), "CPU_ON64 must succeed");
        SMP_TEST_ASSERT(static_cast<std::int64_t>(cpu0.read_register(0)) == PSCIService::kSuccess, "Boot return must be SUCCESS");
        SMP_TEST_ASSERT(psci->get_core_state(c) == CorePowerState::On, "Core state must be ON");
    }

    // Reset CPU0 PC to 0x4000 (after SMC calls which incremented PC)
    cpu0.set_program_counter(0x4000ULL);

    // Run deterministic multi-core round-robin execution for 120 steps (20 steps per core)
    std::size_t executed = cluster.run_deterministic(120);
    SMP_TEST_ASSERT(executed >= 6, "All 6 cores must participate in round-robin execution");

    for (std::size_t c = 0; c < 6; ++c) {
        SMP_TEST_ASSERT(cluster.get_core(c).get_instruction_retirement_count() > 0, "Each core must retire instructions");
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Multi-Core Exclusive Monitor & Cache Line Invalidation
// -----------------------------------------------------------------------------
bool test_multicore_exclusive_monitor() {
    std::cout << "[RUN] Test 7: Multi-Core Exclusive Monitor Invalidation ... ";

    SMPCluster cluster(6, 0x100000);
    constexpr std::uint64_t kSharedLockAddr = 0x8000ULL;

    // Core 0 sets exclusive reservation on kSharedLockAddr
    cluster.register_exclusive_reservation(0, kSharedLockAddr, 8);

    // Verify Core 0 can atomically test and clear reservation
    SMP_TEST_ASSERT(cluster.check_and_clear_exclusive_reservation(0, kSharedLockAddr, 8), "Core 0 reservation must be valid");

    // Re-register Core 0
    cluster.register_exclusive_reservation(0, kSharedLockAddr, 8);

    // Core 1 (or other core) stores to the same address -> invalidates Core 0 reservation
    cluster.invalidate_exclusive_reservations(1, kSharedLockAddr, 8);

    // Core 0 STXR should now fail
    SMP_TEST_ASSERT(!cluster.check_and_clear_exclusive_reservation(0, kSharedLockAddr, 8), "Core 0 reservation must be invalidated by store from Core 1");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Apple PMGR Power Domains for CPU Clusters
// -----------------------------------------------------------------------------
bool test_pmgr_cpu_cluster_power_domains() {
    std::cout << "[RUN] Test 8: Apple PMGR CPU Cluster Power Domains ... ";

    ApplePMGR pmgr(0x504D4752u, 0x10000000ULL, 0x00100000ULL, 0x8120u);

    // Domain 0x0100: Everest Performance Cluster
    // Domain 0x0110: Sawtooth Efficiency Cluster
    pmgr.set_domain_power_state(ApplePMGR::kDomainCpuCluster0, 0x0F);
    pmgr.set_domain_power_state(ApplePMGR::kDomainCpuCluster1, 0x0F);

    SMP_TEST_ASSERT(pmgr.get_domain_power_state(ApplePMGR::kDomainCpuCluster0) == 0x0F, "Everest cluster power state must be 0x0F");
    SMP_TEST_ASSERT(pmgr.get_domain_power_state(ApplePMGR::kDomainCpuCluster1) == 0x0F, "Sawtooth cluster power state must be 0x0F");

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Apple DeviceTree /cpus Representation
// -----------------------------------------------------------------------------
bool test_devicetree_cpus_node() {
    std::cout << "[RUN] Test 9: Apple DeviceTree /cpus Node Conformance ... ";

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size(), &err);
    SMP_TEST_ASSERT(root != nullptr, "DeviceTree root must exist");

    const auto* cpus = root->find_child("cpus");
    SMP_TEST_ASSERT(cpus != nullptr, "/cpus node must exist");

    // Must contain 6 children: cpu0 through cpu5
    for (int i = 0; i < 6; ++i) {
        std::string cpu_name = "cpu" + std::to_string(i);
        const auto* cpu_node = cpus->find_child(cpu_name);
        SMP_TEST_ASSERT(cpu_node != nullptr, "CPU node must exist in DTB");

        const auto* comp = cpu_node->find_property("compatible");
        SMP_TEST_ASSERT(comp != nullptr, "CPU node must have compatible property");
        const char* comp_str = reinterpret_cast<const char*>(comp->data.data());

        if (i < 2) {
            SMP_TEST_ASSERT(std::string(comp_str) == "apple,everest", "cpu0 and cpu1 must be apple,everest");
        } else {
            SMP_TEST_ASSERT(std::string(comp_str) == "apple,sawtooth", "cpu2..cpu5 must be apple,sawtooth");
        }
    }

    std::cout << "PASS\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Darwin Platform Expert CPU Driver Matching
// -----------------------------------------------------------------------------
bool test_darwin_platform_expert_cpu_matching() {
    std::cout << "[RUN] Test 10: Darwin Platform Expert CPU Driver Matching ... ";

    DarwinPlatformExpert pe;
    pe.load_default_iphone15_personalities();

    auto dtb_bytes = AppleDeviceTreeBuilder::create_iphone15_devicetree();
    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dtb_bytes.data(), dtb_bytes.size(), &err);
    SMP_TEST_ASSERT(root != nullptr, "DeviceTree root must exist");

    SMP_TEST_ASSERT(pe.probe_and_match(*root, err), "Driver probing must succeed");

    // Verify platform readiness reports 6 cores verified
    std::string diagnostic;
    SMP_TEST_ASSERT(pe.verify_platform_readiness(diagnostic), "Platform readiness verification must succeed");
    SMP_TEST_ASSERT(diagnostic.find("6 cores verified") != std::string::npos, "Diagnostic must confirm 6 cores (2 Everest + 4 Sawtooth)");

    std::cout << "PASS\n";
    return true;
}

} // anonymous namespace

int main() {
    try {
        std::cout << "=================================================================\n" << std::flush;
        std::cout << " Stage 111: SMP Secondary Core Boot & PSCI Conformance Suite    \n" << std::flush;
        std::cout << "=================================================================\n" << std::flush;

        int passed = 0;
        int total = 10;

        if (test_a16_cluster_instantiation()) ++passed; std::cout << std::flush;
        if (test_psci_version_and_features()) ++passed; std::cout << std::flush;
        if (test_secondary_core_startup()) ++passed; std::cout << std::flush;
        if (test_affinity_info_queries()) ++passed; std::cout << std::flush;
        if (test_psci_error_conformance()) ++passed; std::cout << std::flush;
        if (test_full_heterogeneous_6core_boot()) ++passed; std::cout << std::flush;
        if (test_multicore_exclusive_monitor()) ++passed; std::cout << std::flush;
        if (test_pmgr_cpu_cluster_power_domains()) ++passed; std::cout << std::flush;
        if (test_devicetree_cpus_node()) ++passed; std::cout << std::flush;
        if (test_darwin_platform_expert_cpu_matching()) ++passed; std::cout << std::flush;

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
