// ============================================================================
// Stage 215.2: Execution Provenance and Trace Improvement Tests
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "kernel/guest_trace_logger.h"

using namespace winios::kernel;

namespace {

int g_failures = 0;

#define PROV_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[PROV_FAIL] Line " << __LINE__ << ": " << (msg) << "\n"; \
            ++g_failures; \
        } else { \
            std::cout << "  [PROV_OK] " << (msg) << "\n"; \
        } \
    } while (0)

void test_execution_provenance_trace() {
    std::cout << "[Stage 215.2] Verifying Execution Provenance Trace System...\n";

    auto& logger = GuestTraceLogger::instance();
    logger.reset();

    // 1. Log guest instructions with PID, TID, and EL
    logger.log_instruction(1, "guest_shell", 0x1000, 0xD2800000, 1, 101, 0); // EL0
    logger.log_instruction(1, "guest_shell", 0x1004, 0xD4000001, 2, 101, 0); // EL0 SVC
    PROV_CHECK(logger.instruction_records().size() == 2, "Logged 2 instruction samples");
    PROV_CHECK(logger.instruction_records()[0].tid == 101, "TID recorded correctly");
    PROV_CHECK(logger.instruction_records()[0].exception_level == 0, "Exception level EL0 recorded");
    PROV_CHECK(logger.instruction_records()[0].host_time_ns > 0, "Host clock timestamp populated");

    // 2. Log boundary transition: EL0 Guest -> EL1 Host trap
    logger.log_boundary_transition(1, 101, ExecutionDomain::GuestARM64, ExecutionDomain::HostRuntime,
                                   "SVC_Trap", 0x1004, 1);
    PROV_CHECK(logger.boundary_records().size() == 1, "Logged boundary transition");
    PROV_CHECK(logger.boundary_records()[0].from_domain == ExecutionDomain::GuestARM64, "From domain GuestARM64");
    PROV_CHECK(logger.boundary_records()[0].to_domain == ExecutionDomain::HostRuntime, "To domain HostRuntime");

    // 3. Log syscall with explicit ABI provenance
    std::uint64_t args[6] = {50, 150, 700, 900, 0, 0};
    logger.log_syscall(1, "guest_shell", 500, "SYS_winios_window_create", args, 0x80, 1, 0,
                       "Shell window", "CategoryC_v1.0", 101);
    PROV_CHECK(logger.syscall_records().size() == 1, "Logged syscall with ABI provenance");
    PROV_CHECK(logger.syscall_records()[0].abi_identifier == "CategoryC_v1.0", "ABI identifier CategoryC_v1.0");
    PROV_CHECK(logger.syscall_records()[0].exception_level == 1, "Syscall intercepted at EL1");

    // 4. Log resource ownership
    logger.log_resource_ownership(1, "Window", 1, "Allocated");
    logger.log_resource_ownership(1, "Surface", 1, "Allocated");
    PROV_CHECK(logger.resource_records().size() == 2, "Logged resource ownership records");
    PROV_CHECK(logger.resource_records()[0].owner_pid == 1, "Owner PID recorded");
    PROV_CHECK(logger.resource_records()[0].resource_type == "Window", "Resource type Window");

    // 5. Log input event with origin tracking
    logger.log_input(1, 1, 200, 490, 150, 340, 1, 10000ULL, "Win32Mouse", 101);
    PROV_CHECK(logger.input_records().size() == 1, "Logged input event");
    PROV_CHECK(logger.input_records()[0].input_origin == "Win32Mouse", "Input origin Win32Mouse");

    // 6. Log boundary return: Host -> Guest
    logger.log_boundary_transition(1, 101, ExecutionDomain::HostRuntime, ExecutionDomain::GuestARM64,
                                   "ERET_Return", 0x1008, 0);
    PROV_CHECK(logger.boundary_records().size() == 2, "Logged return boundary transition");
    PROV_CHECK(logger.boundary_records()[1].from_domain == ExecutionDomain::HostRuntime, "From HostRuntime");
    PROV_CHECK(logger.boundary_records()[1].to_domain == ExecutionDomain::GuestARM64, "To GuestARM64");

    // 7. Verify comprehensive markdown export
    std::string md = logger.export_markdown_correlation();
    PROV_CHECK(!md.empty(), "Exported markdown provenance report");
    PROV_CHECK(md.find("Execution Domain Boundary Transitions") != std::string::npos, "Report contains boundary transitions");
    PROV_CHECK(md.find("CategoryC_v1.0") != std::string::npos, "Report contains ABI identifier");
    PROV_CHECK(md.find("Win32Mouse") != std::string::npos, "Report contains input origin");
    PROV_CHECK(md.find("Resource Ownership Audit Trail") != std::string::npos, "Report contains resource audit trail");
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << " WinIOS Stage 215.2: Execution Provenance Tests     \n";
    std::cout << "====================================================\n";

    test_execution_provenance_trace();

    std::cout << "\nResults: " << (g_failures == 0 ? "ALL PROVENANCE TESTS PASSED" : "PROVENANCE FAILURES DETECTED")
              << " (" << g_failures << " failures)\n";
    return g_failures == 0 ? 0 : 1;
}
