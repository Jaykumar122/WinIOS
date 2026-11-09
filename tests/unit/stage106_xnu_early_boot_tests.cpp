#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

#include "device/apple_devicetree.h"
#include "runtime/apple_kernel_execution_engine.h"
#include "runtime/apple_xnu_early_boot.h"

using namespace winios;
using namespace winios::runtime;
using namespace winios::device;

static int g_test_count = 0;
static int g_pass_count = 0;

#define STAGE106_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[-] FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

#define RUN_TEST(name, func) \
    do { \
        g_test_count++; \
        std::cout << "[RUN] Test " << g_test_count << ": " << name << " ... "; \
        if (func()) { \
            g_pass_count++; \
            std::cout << "PASS\n"; \
        } else { \
            std::cout << "FAIL\n"; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: XNU Startup Sequence Source Mapping
// -----------------------------------------------------------------------------
bool test_xnu_startup_sequence_source_mapping() {
    auto map = XnuSymbolMap::create_standard_xnu_arm64_map();
    STAGE106_ASSERT(map.symbol_count() >= 10, "Standard map must contain at least 10 canonical symbols");

    std::string sym;
    uint64_t offset = 0;
    XnuSourceLocation loc;

    // 1. _start at 0xFFFFFE0007004000
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004000ULL, &sym, &offset, &loc), "Must find _start");
    STAGE106_ASSERT(sym == "_start", "_start symbol match");
    STAGE106_ASSERT(loc.source_file == "osfmk/arm64/start.s", "Source file match for _start");
    STAGE106_ASSERT(loc.line_number == 112, "Line number match for _start");

    // 2. arm_vm_init at 0xFFFFFE0007004180
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004180ULL, &sym, &offset, &loc), "Must find arm_vm_init");
    STAGE106_ASSERT(sym == "arm_vm_init", "arm_vm_init symbol match");
    STAGE106_ASSERT(loc.source_file == "osfmk/arm64/arm_vm_init.c", "Source file match for arm_vm_init");

    // 3. arm_init at 0xFFFFFE0007004380
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004380ULL, &sym, &offset, &loc), "Must find arm_init");
    STAGE106_ASSERT(sym == "arm_init", "arm_init symbol match");
    STAGE106_ASSERT(loc.source_file == "osfmk/arm64/arm_init.c", "Source file match for arm_init");

    // 4. PE_init_platform at 0xFFFFFE0007004500
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004500ULL, &sym, &offset, &loc), "Must find PE_init_platform");
    STAGE106_ASSERT(sym == "PE_init_platform", "PE_init_platform symbol match");
    STAGE106_ASSERT(loc.source_file == "pexpert/pexpert/arm/pe_init.c", "Source file match for PE_init_platform");

    // 5. kernel_bootstrap at 0xFFFFFE0007004680
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004680ULL, &sym, &offset, &loc), "Must find kernel_bootstrap");
    STAGE106_ASSERT(sym == "kernel_bootstrap", "kernel_bootstrap symbol match");
    STAGE106_ASSERT(loc.source_file == "osfmk/kern/startup.c", "Source file match for kernel_bootstrap");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Symbol Lookup and Offset Resolution
// -----------------------------------------------------------------------------
bool test_symbol_lookup_and_offset_resolution() {
    auto map = XnuSymbolMap::create_standard_xnu_arm64_map();

    std::string sym;
    uint64_t offset = 0;
    XnuSourceLocation loc;

    // Lookup 0xFFFFFE0007004014 (_start + 0x14)
    STAGE106_ASSERT(map.lookup_address(0xFFFFFE0007004014ULL, &sym, &offset, &loc), "Must resolve offset in _start");
    STAGE106_ASSERT(sym == "_start", "Symbol match");
    STAGE106_ASSERT(offset == 0x14, "Offset match (0x14)");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Missing Symbol Graceful Fallback
// -----------------------------------------------------------------------------
bool test_missing_symbol_graceful_fallback() {
    XnuSymbolMap empty_map;
    std::string sym;
    uint64_t offset = 0;
    XnuSourceLocation loc;

    STAGE106_ASSERT(!empty_map.lookup_address(0x12345678ULL, &sym, &offset, &loc), "Lookup on empty map must return false");

    // Observer must handle missing symbols gracefully
    AppleXnuEarlyBootObserver observer(empty_map);
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.initial_state.pc = 0x12345678ULL;
    ev.initial_state.current_el = 1;
    ev.instructions_retired = 1;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x1000);
    STAGE106_ASSERT(rep.observation_success, "Analysis must succeed even without symbols");
    STAGE106_ASSERT(!rep.milestones_achieved.empty(), "Milestone must be recorded");
    STAGE106_ASSERT(rep.milestones_achieved[0].symbol_at_pc.find("0x12345678") != std::string::npos,
                    "Must fall back to raw PC string");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Cold Boot Entry Milestone Detection
// -----------------------------------------------------------------------------
bool test_cold_boot_entry_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.initial_state.pc = 0xFFFFFE0007004000ULL;
    ev.initial_state.x[0] = 0x43F80000ULL; // struct boot_args
    ev.initial_state.current_el = 1;
    ev.instructions_retired = 5;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x1000);
    STAGE106_ASSERT(rep.count_achieved >= 1, "Must achieve at least ColdBootEntry");
    STAGE106_ASSERT(rep.milestones_achieved[0].milestone == XnuEarlyBootMilestone::ColdBootEntry, "ColdBootEntry match");
    STAGE106_ASSERT(rep.milestones_achieved[0].pc == 0xFFFFFE0007004000ULL, "PC match");
    STAGE106_ASSERT(rep.milestones_achieved[0].current_el == 1, "EL match (EL1)");

    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Boot Args Consumed Milestone Detection
// -----------------------------------------------------------------------------
bool test_boot_args_consumed_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.boot_args_phys_addr = 0x43F80000ULL;
    ev.final_state.x[19] = 0x43F80000ULL;
    ev.final_state.x[22] = 0xFFFFFE0007004000ULL; // virtBase
    ev.final_state.x[23] = 0x40000000ULL;         // physBase
    ev.final_state.x[24] = 64ULL * 1024 * 1024;    // memSize
    ev.final_state.x[26] = 1;                     // bootFlags
    ev.instructions_retired = 10;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x1000);
    bool found_boot_args = false;
    for (const auto& m : rep.milestones_achieved) {
        if (m.milestone == XnuEarlyBootMilestone::BootArgsConsumed) {
            found_boot_args = true;
            STAGE106_ASSERT(m.diagnostic_evidence.find("virtBase") != std::string::npos, "Evidence must cite virtBase");
            break;
        }
    }
    STAGE106_ASSERT(found_boot_args, "BootArgsConsumed milestone must be detected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 6: DeviceTree Inspected Milestone Detection
// -----------------------------------------------------------------------------
bool test_devicetree_inspected_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.final_state.x[5] = 0x48000000ULL;

    AppleMemoryAccessRecord mem_read;
    mem_read.is_write = false;
    mem_read.physical_address = 0x48000000ULL;
    mem_read.size = 4;
    mem_read.value = 0x61647473u;
    ev.memory_access_trace.push_back(mem_read);
    ev.instructions_retired = 12;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    bool found_dtb = false;
    for (const auto& m : rep.milestones_achieved) {
        if (m.milestone == XnuEarlyBootMilestone::DeviceTreeInspected) {
            found_dtb = true;
            STAGE106_ASSERT(m.diagnostic_evidence.find("DeviceTree") != std::string::npos, "Evidence must cite DeviceTree");
            break;
        }
    }
    STAGE106_ASSERT(found_dtb, "DeviceTreeInspected milestone must be detected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Bootstrap Stack Active Milestone Detection
// -----------------------------------------------------------------------------
bool test_bootstrap_stack_active_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.initial_state.sp = 0x43F00000ULL;
    ev.final_state.sp = 0x43F00000ULL;

    AppleMemoryAccessRecord stack_write;
    stack_write.is_write = true;
    stack_write.physical_address = 0x43F00000ULL;
    stack_write.size = 8;
    stack_write.value = 0x1234;
    ev.memory_access_trace.push_back(stack_write);
    ev.instructions_retired = 15;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    bool found_stack = false;
    for (const auto& m : rep.milestones_achieved) {
        if (m.milestone == XnuEarlyBootMilestone::BootstrapStackActive) {
            found_stack = true;
            break;
        }
    }
    STAGE106_ASSERT(found_stack, "BootstrapStackActive milestone must be detected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Early Sysreg Configured Milestone Detection
// -----------------------------------------------------------------------------
bool test_early_sysreg_configured_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.final_state.cpacr_el1 = 0x00300000u; // FP/SIMD enabled
    ev.instructions_retired = 20;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    bool found_sysreg = false;
    for (const auto& m : rep.milestones_achieved) {
        if (m.milestone == XnuEarlyBootMilestone::EarlySysregConfigured) {
            found_sysreg = true;
            STAGE106_ASSERT(m.diagnostic_evidence.find("CPACR_EL1") != std::string::npos, "Evidence must cite CPACR_EL1");
            break;
        }
    }
    STAGE106_ASSERT(found_sysreg, "EarlySysregConfigured milestone must be detected");

    return true;
}

// -----------------------------------------------------------------------------
// Test 9: Fault Halted Milestone Detection
// -----------------------------------------------------------------------------
bool test_fault_halted_milestone_detection() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.termination_reason = TerminationReason::SynchronousException;
    ev.final_state.pc = 0xFFFFFE0007004020ULL;
    ev.termination_detail = "MMU Page Translation Fault at address 0x50000000";
    ev.instructions_retired = 8;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    STAGE106_ASSERT(rep.fault_occurred, "rep.fault_occurred must be true");
    STAGE106_ASSERT(rep.furthest_milestone == XnuEarlyBootMilestone::FaultHalted, "Furthest milestone must be FaultHalted");
    STAGE106_ASSERT(rep.fault_summary.find("Fault halted") != std::string::npos, "Fault summary must describe fault");

    return true;
}

// -----------------------------------------------------------------------------
// Test 10: Strict Honesty Synthetic Milestone Isolation
// -----------------------------------------------------------------------------
bool test_strict_honesty_synthetic_milestone_isolation() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.kernel_classification = KernelClassification::SyntheticAppleBootContract;
    ev.instructions_retired = 23;
    ev.initial_state.pc = 0xFFFFFE0007004000ULL;
    ev.final_state.pc = 0xFFFFFE000700405CULL;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    STAGE106_ASSERT(!rep.genuine_xnu_execution, "Synthetic kernel must NEVER be labeled genuine_xnu_execution");
    for (const auto& m : rep.milestones_achieved) {
        STAGE106_ASSERT(m.is_synthetic, "All synthetic milestones must have is_synthetic = true");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 11: Genuine Encrypted Kernel Blocker Handling
// -----------------------------------------------------------------------------
bool test_genuine_encrypted_kernel_blocker() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = false;
    ev.kernel_classification = KernelClassification::GenuineEncrypted_Blocked;
    ev.blocker_reason = "Kernelcache is encrypted with Apple SEP hardware AES keys. Execution blocked.";
    ev.instructions_retired = 0;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    STAGE106_ASSERT(!rep.genuine_xnu_execution, "Encrypted kernel cannot achieve genuine execution");
    STAGE106_ASSERT(!rep.observation_success, "Setup must reflect blocked state");
    STAGE106_ASSERT(!rep.blocker_description.empty(), "Blocker description must be preserved");

    return true;
}

// -----------------------------------------------------------------------------
// Test 12: End-to-End Synthetic Early Boot Execution
// -----------------------------------------------------------------------------
bool test_end_to_end_synthetic_early_boot_execution() {
    auto dtb = AppleDeviceTreeBuilder::create_iphone15_devicetree();

    AppleKernelExecutionConfig cfg;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 64ULL * 1024 * 1024;
    cfg.boot_stack_phys = 0x43F00000ULL;
    cfg.boot_args_phys  = 0x43F80000ULL;
    cfg.devicetree_phys = 0x43E00000ULL;
    cfg.devicetree_blob = dtb;
    cfg.force_synthetic = true;
    cfg.record_memory_loads = true;
    cfg.kernel_data = AppleXnuEarlyBootObserver::create_synthetic_early_boot_kernel();

    AppleKernelExecutionEngine engine(cfg);
    STAGE106_ASSERT(engine.initialize(), "Initialize engine");

    auto ev = engine.run();
    STAGE106_ASSERT(ev.setup_success, "Execution setup must succeed");
    STAGE106_ASSERT(ev.termination_reason == TerminationReason::Halted_WFI, "Kernel must halt at WFI");
    STAGE106_ASSERT(ev.final_state.x[0] == 0, "X0 must be 0 (success status)");
    STAGE106_ASSERT(ev.final_state.x[20] == 0x504C4150ULL, "X20 must hold 'APPL' token");

    AppleXnuEarlyBootObserver observer;
    auto rep = observer.analyze_execution(ev, cfg.devicetree_phys, static_cast<std::uint32_t>(dtb.size()));

    STAGE106_ASSERT(rep.observation_success, "Observation must succeed");
    STAGE106_ASSERT(rep.count_achieved >= 4, "Must achieve at least 4 early boot milestones");
    STAGE106_ASSERT(!rep.formatted_markdown_report.empty(), "Markdown report must not be empty");
    STAGE106_ASSERT(rep.formatted_markdown_report.find("SYNTHETIC TEST CONTRACT") != std::string::npos,
                    "Report must honestly declare synthetic status");

    return true;
}

// -----------------------------------------------------------------------------
// Test 13: Milestone Name Conversion
// -----------------------------------------------------------------------------
bool test_milestone_name_conversion() {
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::ColdBootEntry)) == "ColdBootEntry", "ColdBootEntry");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::BootArgsConsumed)) == "BootArgsConsumed", "BootArgsConsumed");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::DeviceTreeInspected)) == "DeviceTreeInspected", "DeviceTreeInspected");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::BootstrapStackActive)) == "BootstrapStackActive", "BootstrapStackActive");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::EarlySysregConfigured)) == "EarlySysregConfigured", "EarlySysregConfigured");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::EarlyVmInit)) == "EarlyVmInit", "EarlyVmInit");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::ArmInitTransition)) == "ArmInitTransition", "ArmInitTransition");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::PlatformExpertInit)) == "PlatformExpertInit", "PlatformExpertInit");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::ConsoleInitialized)) == "ConsoleInitialized", "ConsoleInitialized");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::KernelBootstrap)) == "KernelBootstrap", "KernelBootstrap");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::MachineStartup)) == "MachineStartup", "MachineStartup");
    STAGE106_ASSERT(std::string(AppleXnuEarlyBootObserver::milestone_name(XnuEarlyBootMilestone::FaultHalted)) == "FaultHalted", "FaultHalted");

    return true;
}

// -----------------------------------------------------------------------------
// Test 14: Milestone Chronological Ordering
// -----------------------------------------------------------------------------
bool test_milestone_chronological_ordering() {
    AppleXnuEarlyBootObserver observer;
    AppleKernelExecutionEvidence ev;
    ev.setup_success = true;
    ev.boot_args_phys_addr = 0x43F80000ULL;
    ev.initial_state.pc = 0xFFFFFE0007004000ULL;
    ev.initial_state.current_el = 1;
    ev.final_state.x[19] = 0x43F80000ULL;
    ev.final_state.x[22] = 0xFFFFFE0007004000ULL;
    ev.final_state.x[23] = 0x40000000ULL;
    ev.final_state.x[24] = 64ULL * 1024 * 1024;
    ev.final_state.x[26] = 1;
    ev.final_state.sp = 0x43F00000ULL;
    ev.initial_state.sp = 0x43F00000ULL;
    ev.instructions_retired = 25;

    auto rep = observer.analyze_execution(ev, 0x48000000ULL, 0x2000);
    for (std::size_t i = 1; i < rep.milestones_achieved.size(); ++i) {
        STAGE106_ASSERT(rep.milestones_achieved[i].instruction_count >= rep.milestones_achieved[i - 1].instruction_count,
                        "Milestones must be monotonically non-decreasing in instruction count");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Test 15: Observer Reset Functionality
// -----------------------------------------------------------------------------
bool test_observer_reset_functionality() {
    AppleXnuEarlyBootObserver observer;
    XnuSymbolMap custom_map;
    custom_map.add_symbol("custom_entry", 0x1000, 0x100, {"custom_entry", "custom.s", 1, "test"});
    observer.set_symbol_map(custom_map);
    STAGE106_ASSERT(observer.symbol_map().symbol_count() == 1, "Custom map has 1 symbol");

    observer.reset();
    STAGE106_ASSERT(observer.symbol_map().symbol_count() >= 10, "Reset restores standard XNU map");

    return true;
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "=================================================================\n";
    std::cout << " Stage 106: XNU Early-Boot Milestone Observation Test Suite     \n";
    std::cout << "=================================================================\n";

    RUN_TEST("XNU startup sequence source mapping", test_xnu_startup_sequence_source_mapping);
    RUN_TEST("Symbol lookup and offset resolution", test_symbol_lookup_and_offset_resolution);
    RUN_TEST("Missing symbol graceful fallback", test_missing_symbol_graceful_fallback);
    RUN_TEST("Cold boot entry milestone detection", test_cold_boot_entry_milestone_detection);
    RUN_TEST("Boot args consumed milestone detection", test_boot_args_consumed_milestone_detection);
    RUN_TEST("DeviceTree inspected milestone detection", test_devicetree_inspected_milestone_detection);
    RUN_TEST("Bootstrap stack active milestone detection", test_bootstrap_stack_active_milestone_detection);
    RUN_TEST("Early sysreg configured milestone detection", test_early_sysreg_configured_milestone_detection);
    RUN_TEST("Fault halted milestone detection", test_fault_halted_milestone_detection);
    RUN_TEST("Strict honesty synthetic milestone isolation", test_strict_honesty_synthetic_milestone_isolation);
    RUN_TEST("Genuine encrypted kernel blocker handling", test_genuine_encrypted_kernel_blocker);
    RUN_TEST("End-to-end synthetic early boot execution", test_end_to_end_synthetic_early_boot_execution);
    RUN_TEST("Milestone name conversion", test_milestone_name_conversion);
    RUN_TEST("Milestone chronological ordering", test_milestone_chronological_ordering);
    RUN_TEST("Observer reset functionality", test_observer_reset_functionality);

    std::cout << "=================================================================\n";
    std::cout << " Summary: " << g_pass_count << " / " << g_test_count << " tests passed.\n";
    std::cout << "=================================================================\n";

    return (g_pass_count == g_test_count) ? 0 : 1;
}
