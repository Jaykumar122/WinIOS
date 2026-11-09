#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "runtime/apple_boot_dependency_graph.h"

using namespace winios::runtime;

#define STAGE98_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 98 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: DAG Initialization and Acyclicity
// -----------------------------------------------------------------------------
static bool test_dag_initialization_and_acyclicity() {
    std::cout << "\n[1] test_dag_initialization_and_acyclicity...\n";

    AppleBootDependencyGraph graph;
    STAGE98_ASSERT(graph.total_nodes() == 14, "Must contain exactly 14 boot sequence nodes");
    STAGE98_ASSERT(graph.is_dag_valid(), "Boot graph must be a valid Directed Acyclic Graph (DAG)");

    std::vector<BootNodeId> order;
    STAGE98_ASSERT(graph.get_topological_order(order), "Topological sort must succeed");
    STAGE98_ASSERT(order.size() == 14, "Topological order must include all 14 nodes");

    // Verify root is HardwareProfile_Init
    STAGE98_ASSERT(order.front() == BootNodeId::HardwareProfile_Init, "Root must be HardwareProfile_Init");

    // Verify that for every node in topological order, all its dependencies appeared earlier
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto* node = graph.get_node(order[i]);
        STAGE98_ASSERT(node != nullptr, "Node must exist");
        for (BootNodeId dep : node->dependencies) {
            bool found_prior = false;
            for (std::size_t j = 0; j < i; ++j) {
                if (order[j] == dep) {
                    found_prior = true;
                    break;
                }
            }
            STAGE98_ASSERT(found_prior, "Topological order invariant violated: prerequisite appeared after node");
        }
    }

    std::cout << "[PASS] DAG Initialization and Acyclicity Verified (14 nodes, topological sort valid)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Prerequisite Enforcement & Out-of-Order Rejection
// -----------------------------------------------------------------------------
static bool test_prerequisite_enforcement() {
    std::cout << "\n[2] test_prerequisite_enforcement...\n";

    AppleBootDependencyGraph graph;

    // Attempting to jump directly to Kernel_Entry_Execution without prerequisites
    std::string err;
    bool ok = graph.mark_satisfied(BootNodeId::Kernel_Entry_Execution, 100, &err);
    STAGE98_ASSERT(!ok, "Out-of-order execution must be rejected");
    STAGE98_ASSERT(!err.empty(), "Error reason must explain rejection");
    STAGE98_ASSERT(graph.get_state(BootNodeId::Kernel_Entry_Execution) == BootNodeState::Blocked,
                   "Rejected node must transition to Blocked");

    std::cout << "Rejection Diagnostic: " << err << "\n";
    std::cout << "[PASS] Prerequisite Enforcement & Out-of-Order Rejection\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Sequential Linear Progression
// -----------------------------------------------------------------------------
static bool test_linear_progression() {
    std::cout << "\n[3] test_linear_progression...\n";

    AppleBootDependencyGraph graph;

    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::HardwareProfile_Init, 1), "HardwareProfile_Init");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::PhysicalMemory_Map, 2), "PhysicalMemory_Map");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::AIC_InterruptController_Init, 3), "AIC_InterruptController_Init");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::DeviceTree_Ingestion, 4), "DeviceTree_Ingestion");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::KernelCache_Admission, 5), "KernelCache_Admission");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::BootArgs_Preparation, 6), "BootArgs_Preparation");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::Kernel_Entry_Execution, 7), "Kernel_Entry_Execution");

    STAGE98_ASSERT(graph.satisfied_count() == 7, "7 nodes must be satisfied");
    STAGE98_ASSERT(graph.is_satisfied(BootNodeId::Kernel_Entry_Execution), "Kernel_Entry_Execution satisfied");

    std::cout << "[PASS] Sequential Linear Progression (7 early boot nodes satisfied)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Failure Cascading to Dependent Subsystems
// -----------------------------------------------------------------------------
static bool test_failure_cascading() {
    std::cout << "\n[4] test_failure_cascading...\n";

    AppleBootDependencyGraph graph;
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::HardwareProfile_Init, 1), "HardwareProfile_Init");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::PhysicalMemory_Map, 2), "PhysicalMemory_Map");

    // Fail DeviceTree_Ingestion
    graph.mark_failed(BootNodeId::DeviceTree_Ingestion, "ADTS magic corrupt");

    STAGE98_ASSERT(graph.get_state(BootNodeId::DeviceTree_Ingestion) == BootNodeState::Failed,
                   "DeviceTree must be marked Failed");

    // Verify that downstream nodes depending on DeviceTree are cascaded to Blocked
    STAGE98_ASSERT(graph.get_state(BootNodeId::BootArgs_Preparation) == BootNodeState::Blocked,
                   "BootArgs must be Blocked");
    STAGE98_ASSERT(graph.get_state(BootNodeId::Kernel_Entry_Execution) == BootNodeState::Blocked,
                   "Kernel_Entry must be Blocked");

    std::cout << "[PASS] Failure Cascading to Dependent Subsystems\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Independent Parallel Branch Resolution
// -----------------------------------------------------------------------------
static bool test_independent_branch_resolution() {
    std::cout << "\n[5] test_independent_branch_resolution...\n";

    AppleBootDependencyGraph graph;
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::HardwareProfile_Init, 1), "HardwareProfile_Init");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::PhysicalMemory_Map, 2), "PhysicalMemory_Map");

    // Can admit KernelCache before DeviceTree
    STAGE98_ASSERT(graph.can_execute(BootNodeId::KernelCache_Admission), "KernelCache can execute");
    STAGE98_ASSERT(graph.can_execute(BootNodeId::DeviceTree_Ingestion), "DeviceTree can execute");

    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::KernelCache_Admission, 3), "Admit KernelCache first");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::DeviceTree_Ingestion, 4), "Ingest DeviceTree second");

    std::cout << "[PASS] Independent Parallel Branch Resolution\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Full Boot Sequence Culmination to SpringBoard
// -----------------------------------------------------------------------------
static bool test_userspace_and_springboard_culmination() {
    std::cout << "\n[6] test_userspace_and_springboard_culmination...\n";

    AppleBootDependencyGraph graph;

    std::vector<BootNodeId> order;
    STAGE98_ASSERT(graph.get_topological_order(order), "Topological order");

    std::uint64_t step = 1;
    for (BootNodeId id : order) {
        std::string err;
        bool ok = graph.mark_satisfied(id, step++, &err);
        STAGE98_ASSERT(ok, ("Failed to satisfy node " + std::string(AppleBootDependencyGraph::node_to_string(id)) + ": " + err).c_str());
    }

    STAGE98_ASSERT(graph.satisfied_count() == 14, "All 14 boot nodes must be satisfied");
    STAGE98_ASSERT(graph.is_satisfied(BootNodeId::SpringBoard_Display), "SpringBoard must be reached");

    std::cout << "[PASS] Full Boot Sequence Culmination to SpringBoard (14/14 Milestones Complete)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Mermaid Graph Generation
// -----------------------------------------------------------------------------
static bool test_mermaid_graph_generation() {
    std::cout << "\n[7] test_mermaid_graph_generation...\n";

    AppleBootDependencyGraph graph;
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::HardwareProfile_Init, 1), "HardwareProfile_Init");
    STAGE98_ASSERT(graph.mark_satisfied(BootNodeId::PhysicalMemory_Map, 2), "PhysicalMemory_Map");

    std::string mermaid = graph.generate_mermaid_graph();
    STAGE98_ASSERT(!mermaid.empty(), "Mermaid output must not be empty");
    STAGE98_ASSERT(mermaid.find("flowchart TD") != std::string::npos, "Must contain flowchart TD");
    STAGE98_ASSERT(mermaid.find("HardwareProfile_Init") != std::string::npos, "Must contain node name");
    STAGE98_ASSERT(mermaid.find("Satisfied") != std::string::npos, "Must show Satisfied state");
    STAGE98_ASSERT(mermaid.find("-->") != std::string::npos, "Must contain dependency arrows");

    std::cout << "[PASS] Mermaid Graph Generation\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Dependency Report Generation
// -----------------------------------------------------------------------------
static bool test_dependency_report_generation() {
    std::cout << "\n[8] test_dependency_report_generation...\n";

    AppleBootDependencyGraph graph;
    std::string report = graph.generate_dependency_report();

    STAGE98_ASSERT(!report.empty(), "Report must not be empty");
    STAGE98_ASSERT(report.find("# iPhone 15 Native iOS Boot Dependency Execution Report") != std::string::npos,
                   "Header must be present");
    STAGE98_ASSERT(report.find("SpringBoard_Display") != std::string::npos, "Final node must be present");
    STAGE98_ASSERT(report.find("| Boot Stage Node | State |") != std::string::npos, "Table header must be present");

    std::cout << "[PASS] Dependency Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 98: Boot Dependency Graph Tests\n";
    std::cout << "========================================================\n";

    if (!test_dag_initialization_and_acyclicity()) return 1;
    if (!test_prerequisite_enforcement()) return 1;
    if (!test_linear_progression()) return 1;
    if (!test_failure_cascading()) return 1;
    if (!test_independent_branch_resolution()) return 1;
    if (!test_userspace_and_springboard_culmination()) return 1;
    if (!test_mermaid_graph_generation()) return 1;
    if (!test_dependency_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 98 BOOT DEPENDENCY GRAPH SUITES PASSED!\n";
    return 0;
}
