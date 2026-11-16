#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/execution_diagnostics.h"

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

void test_kernel_boot_timeline_logging() {
    std::cout << "[Test 1] Kernel Boot Timeline Phase Logging..." << std::endl;

    ExecutionDiagnostics diag;
    TEST_CHECK(diag.kernel_boot_event_count() == 0, "Initial boot event count must be 0");

    diag.log_kernel_boot_phase("FirmwareHandoff", 0, 0x1000ULL, 0x80004000ULL, 0, "Entering EL1 from iBoot");
    diag.log_kernel_boot_phase("EarlyPagingInit", 0, 0x1010ULL, 0x80004000ULL, 4, "TCR 16KB granule programmed");
    diag.log_kernel_boot_phase("DeviceTreeParsed", 0, 0x1028ULL, 0x80004000ULL, 10, "Found compatible apple,d37ap");
    diag.log_kernel_boot_phase("PrimaryCoreOnline", 0, 0x1040ULL, 0x80004000ULL, 16, "Everest Core 0 rendezvous set");
    diag.log_kernel_boot_phase("SecondaryCoreRendezvous", 1, 0x2000ULL, 0x80008000ULL, 8, "Everest Core 1 online");
    diag.log_kernel_boot_phase("PeripheralBoundaryEncountered", 0, 0x1048ULL, 0x80004000ULL, 19, "Data abort at AIC 0x23B100000");

    TEST_CHECK(diag.kernel_boot_event_count() == 6, "Must have recorded 6 boot events");

    const auto* ev0 = diag.get_kernel_boot_event(0);
    TEST_CHECK(ev0 != nullptr, "Event 0 must exist");
    if (ev0) {
        TEST_CHECK(ev0->phase_name == "FirmwareHandoff", "Phase 0 name mismatch");
        TEST_CHECK(ev0->core_id == 0, "Phase 0 core mismatch");
        TEST_CHECK(ev0->pc == 0x1000ULL, "Phase 0 PC mismatch");
        TEST_CHECK(ev0->sp == 0x80004000ULL, "Phase 0 SP mismatch");
        TEST_CHECK(ev0->instructions_retired == 0, "Phase 0 instructions retired mismatch");
    }

    const auto* ev4 = diag.get_kernel_boot_event(4);
    TEST_CHECK(ev4 != nullptr, "Event 4 must exist");
    if (ev4) {
        TEST_CHECK(ev4->phase_name == "SecondaryCoreRendezvous", "Phase 4 name mismatch");
        TEST_CHECK(ev4->core_id == 1, "Phase 4 core mismatch");
        TEST_CHECK(ev4->instructions_retired == 8, "Phase 4 instructions retired mismatch");
    }
}

void test_boot_event_register_diffs() {
    std::cout << "[Test 2] Boot Timeline Event Register Diffs..." << std::endl;

    ExecutionDiagnostics diag;

    KernelBootTimelineEvent ev{};
    ev.phase_name = "BootArgsConstructed";
    ev.core_id = 0;
    ev.pc = 0x1020ULL;
    ev.sp = 0x80004000ULL;
    ev.instructions_retired = 8;
    ev.details = "boot_args Rev 2 at 0x804000000";
    ev.register_diffs = {
        {0, 0x0ULL, 0x804000000ULL},  // X0 set to boot_args PA
        {19, 0x0ULL, 0x804000000ULL}, // X19 callee-saved boot_args
        {20, 0x0ULL, 0x80000000ULL}   // X20 MPIDR_EL1
    };

    diag.log_kernel_boot_event(ev);

    TEST_CHECK(diag.kernel_boot_event_count() == 1, "Must have 1 boot event");
    const auto* logged = diag.get_kernel_boot_event(0);
    TEST_CHECK(logged != nullptr, "Logged event must exist");
    if (logged) {
        TEST_CHECK(logged->register_diffs.size() == 3, "Must have 3 register diffs");
        TEST_CHECK(logged->register_diffs[0].new_value == 0x804000000ULL, "X0 new val mismatch");
        TEST_CHECK(logged->register_diffs[1].new_value == 0x804000000ULL, "X19 new val mismatch");
        TEST_CHECK(logged->register_diffs[2].new_value == 0x80000000ULL, "X20 new val mismatch");
    }
}

void test_json_export_structure() {
    std::cout << "[Test 3] JSON Serialization Structure..." << std::endl;

    ExecutionDiagnostics diag;

    diag.log_kernel_boot_phase("FirmwareHandoff", 0, 0x1000ULL, 0x80004000ULL, 0, "EL1 handoff");
    KernelBootTimelineEvent ev2{};
    ev2.phase_name = "BoundaryTrap";
    ev2.core_id = 0;
    ev2.pc = 0x1048ULL;
    ev2.sp = 0x80004000ULL;
    ev2.instructions_retired = 19;
    ev2.details = "Proprietary Apple AIC MMIO access";
    ev2.register_diffs.push_back({28, 0, 0x25}); // ESR Data Abort
    diag.log_kernel_boot_event(ev2);

    const std::string json = diag.export_kernel_boot_timeline_json();
    TEST_CHECK(!json.empty(), "Exported JSON must not be empty");
    TEST_CHECK(json.find("\"schema_version\": \"205.21\"") != std::string::npos, "JSON must declare schema 205.21");
    TEST_CHECK(json.find("\"target_device\": \"iPhone15,4\"") != std::string::npos, "JSON must declare target device");
    TEST_CHECK(json.find("\"total_events\": 2") != std::string::npos, "JSON must report total_events 2");
    TEST_CHECK(json.find("\"FirmwareHandoff\"") != std::string::npos, "JSON must include FirmwareHandoff");
    TEST_CHECK(json.find("\"BoundaryTrap\"") != std::string::npos, "JSON must include BoundaryTrap");
    TEST_CHECK(json.find("\"register_diffs\"") != std::string::npos, "JSON must include register diffs");
}

void test_reset_and_capacity() {
    std::cout << "[Test 4] Diagnostic Reset and Max Capacity..." << std::endl;

    ExecutionDiagnostics diag;
    diag.set_max_capacity(5);

    for (std::size_t i = 0; i < 10; ++i) {
        diag.log_kernel_boot_phase("Phase" + std::to_string(i), 0, 0x1000ULL + i, 0x8000ULL, i);
    }

    TEST_CHECK(diag.kernel_boot_event_count() == 5, "Capacity limit must bound event count to 5");
    const auto* oldest = diag.get_kernel_boot_event(0);
    TEST_CHECK(oldest != nullptr && oldest->phase_name == "Phase5", "Oldest event must be Phase5 after wrap");

    diag.reset();
    TEST_CHECK(diag.kernel_boot_event_count() == 0, "Reset must clear all boot events");
}

} // namespace

int main() {
    std::cout << "=== Running Stage 205.21 Kernel Boot Diagnostics Tests ===" << std::endl;

    test_kernel_boot_timeline_logging();
    test_boot_event_register_diffs();
    test_json_export_structure();
    test_reset_and_capacity();

    if (g_failures == 0) {
        std::cout << "=== All Stage 205.21 Tests PASSED ===" << std::endl;
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 205.21 Tests FAILED ===" << std::endl;
        return 1;
    }
}
