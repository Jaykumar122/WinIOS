#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_devicetree.h"
#include "device/kernel_boot_devicetree.h"
#include "memory/memory.h"
#include "runtime/smp_kernel_boot_manager.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::device;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_secondary_core_audit_tier1_devicetree() {
    std::cout << "[Test 1] Tier 1: DeviceTree Multi-Core Node Audit...\n";

    KernelBootDeviceTreeConfig cfg;
    cfg.num_cores = 6;
    cfg.p_cores = 2;
    cfg.e_cores = 4;
    auto dt_blob = KernelBootDeviceTreeBuilder::build_apple_devicetree(cfg);
    TEST_CHECK(!dt_blob.empty(), "Device tree blob built");

    std::string err;
    auto root = AppleDeviceTreeBuilder::deserialize(dt_blob.data(), dt_blob.size(), &err);
    TEST_CHECK(root != nullptr, "Deserialization succeeded: " + err);

    auto* cpus = root->find_child("cpus");
    TEST_CHECK(cpus != nullptr, "/cpus node present in DeviceTree");
    TEST_CHECK(cpus->children().size() == 6, "Must have exactly 6 CPU child nodes");

    for (std::size_t i = 0; i < 6; ++i) {
        std::string cpu_name = "cpu" + std::to_string(i);
        auto* cpu_node = cpus->find_child(cpu_name);
        TEST_CHECK(cpu_node != nullptr, "CPU node " + cpu_name + " present");
    }

    std::cout << "  -> DeviceTree multi-core nodes verified.\n";
}

void test_secondary_core_audit_tier2_virtual_smp() {
    std::cout << "[Test 2] Tier 2: Virtual SMP Infrastructure & Rendezvous Audit...\n";

    Memory memory(0x400000); // 4 MB RAM
    SmpKernelBootManager smp;

    constexpr std::size_t kNumCores = 6;
    constexpr std::uint64_t kEntryPC = 0x1000ULL;
    constexpr std::uint64_t kBootArgsPA = 0x40000ULL;
    constexpr std::uint64_t kStackPoolBase = 0x100000ULL;
    constexpr std::uint64_t kVbarBase = 0x2000ULL;

    const bool init_ok = smp.initialize(kNumCores, kEntryPC, kBootArgsPA, kStackPoolBase, kVbarBase);
    TEST_CHECK(init_ok, "SMP initialization must succeed");
    TEST_CHECK(smp.num_cores() == 6, "Total cores must be 6");
    TEST_CHECK(smp.online_cores() == 0, "Initial online cores must be 0");

    // 1. Verify Topology and Per-Core Config
    for (std::size_t i = 0; i < 6; ++i) {
        const auto* core = smp.get_core_config(i);
        TEST_CHECK(core != nullptr, "Core config must exist");
        if (core) {
            if (i == 0) {
                TEST_CHECK(core->is_primary == true, "Core 0 is primary");
                TEST_CHECK(core->is_performance_core == true, "Core 0 is Everest P-core");
            } else if (i == 1) {
                TEST_CHECK(core->is_primary == false, "Core 1 is secondary");
                TEST_CHECK(core->is_performance_core == true, "Core 1 is Everest P-core");
            } else {
                TEST_CHECK(core->is_primary == false, "Core " + std::to_string(i) + " is secondary");
                TEST_CHECK(core->is_performance_core == false, "Core " + std::to_string(i) + " is Sawtooth E-core");
                TEST_CHECK((core->mpidr_el1 & 0x0100ULL) != 0, "Sawtooth core reports cluster 1");
            }
            TEST_CHECK((core->initial_sp % 16) == 0, "Core SP 16-byte aligned");
        }
    }

    // 2. Primary core bring-up
    CPU primary_cpu;
    bool boot_p = smp.boot_primary_core(primary_cpu, memory);
    TEST_CHECK(boot_p, "Primary core boot succeeded");
    TEST_CHECK(smp.online_cores() == 1, "1 core online");
    TEST_CHECK(smp.check_rendezvous(memory) == false, "Rendezvous incomplete with 1 core");

    // 3. Secondary core bring-up
    std::vector<CPU> secondary_cpus(5);
    for (std::size_t i = 1; i < 6; ++i) {
        bool boot_s = smp.bring_up_secondary_core(i, secondary_cpus[i - 1], memory);
        TEST_CHECK(boot_s, "Secondary core " + std::to_string(i) + " bring up succeeded");
    }

    TEST_CHECK(smp.online_cores() == 6, "All 6 cores online");
    TEST_CHECK(smp.check_rendezvous(memory) == true, "Rendezvous complete across all 6 cores");

    std::cout << "  -> Virtual SMP rendezvous verified.\n";
}

void test_secondary_core_audit_tier3_execution() {
    std::cout << "[Test 3] Tier 3: Guest-Side Execution Reality Audit...\n";

    // Reconcile: Public-source start.s single-core bootstrap vs secondary core park
    // On Apple Silicon, secondary cores are parked in firmware (iBoot/SEP) until primary sends IPI.
    // In our bootstrap trace (50+ instructions), execution remains on Core 0.
    Memory memory(0x400000);
    SmpKernelBootManager smp;
    smp.initialize(6, 0x1000ULL, 0x40000ULL, 0x100000ULL, 0x2000ULL);

    CPU primary_cpu;
    smp.boot_primary_core(primary_cpu, memory);

    // Primary CPU executes bootstrap instructions while secondary cores remain offline/parked
    memory.write_u32(0x1000, 0xD503201FU); // NOP
    smp.step_core(0, primary_cpu, memory);

    const auto* core0 = smp.get_core_config(0);
    const auto* core1 = smp.get_core_config(1);
    TEST_CHECK(core0->phase == SmpCoreBootPhase::RendezvousOnline, "Core 0 is active/online");
    TEST_CHECK(core1->phase == SmpCoreBootPhase::HandoffReady, "Core 1 legitimately parked waiting during early start.s");
    TEST_CHECK(smp.online_cores() == 1, "Only primary core is online during early bootstrap");

    std::cout << "  -> Tier 3 single-core execution boundary confirmed.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.20 Secondary-Core Execution Audit Tests ===\n";
    test_secondary_core_audit_tier1_devicetree();
    test_secondary_core_audit_tier2_virtual_smp();
    test_secondary_core_audit_tier3_execution();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.20 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.20 Tests FAILED ===\n";
        return 1;
    }
}
