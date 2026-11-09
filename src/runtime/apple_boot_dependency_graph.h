#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace winios::runtime {

// -----------------------------------------------------------------------------
// BootNodeId: Canonical nodes in the iOS boot sequence DAG
// -----------------------------------------------------------------------------
enum class BootNodeId {
    HardwareProfile_Init,           // iPhone 15 A16 Bionic profile validated
    PhysicalMemory_Map,             // RAM base 0x40000000, MMIO apertures, 16 KB pages
    AIC_InterruptController_Init,   // Apple AIC 2 interrupt controller registered
    DeviceTree_Ingestion,           // Apple DeviceTree ADTS validated & staged
    KernelCache_Admission,          // 64-bit ARM64e kernelcache admitted & verified
    BootArgs_Preparation,           // Darwin AppleBootArgs (Rev 2) constructed in X0
    Kernel_Entry_Execution,         // Guest PC enters kernel entry point (EL1)
    XNU_Early_Init,                 // High-half page tables armed, KASLR applied
    IOKit_Platform_Matching,        // AppleARMPE and driver personalities matched
    RootFS_Mount,                   // Ramdisk or APFS Sealed System Volume mounted
    TrustCache_Load,                // Code signing trust cache registered
    Launchd_Spawn,                  // PID 1 (/sbin/launchd) loaded and executed
    Userspace_Daemons,              // iOS system services spawned (logd, launchd jobs)
    SpringBoard_Display             // Native SpringBoard UI rendered on display
};

// -----------------------------------------------------------------------------
// BootNodeState: Execution status of an individual boot gate
// -----------------------------------------------------------------------------
enum class BootNodeState {
    Pending,    // Waiting for prerequisites to be satisfied
    Satisfied,  // Successfully executed and validated
    Blocked,    // Prerequisites unmet or out-of-order
    Failed      // Step execution resulted in an error
};

// -----------------------------------------------------------------------------
// BootDependencyNode: Metadata and dependency edges for a node
// -----------------------------------------------------------------------------
struct BootDependencyNode {
    BootNodeId id{BootNodeId::HardwareProfile_Init};
    std::string name;
    std::string description;
    BootNodeState state{BootNodeState::Pending};
    std::vector<BootNodeId> dependencies; // Prerequisite node IDs
    std::string failure_reason;
    std::uint64_t timestamp_or_step{0};
};

// -----------------------------------------------------------------------------
// AppleBootDependencyGraph: Enforces topological order and boot prerequisites
// -----------------------------------------------------------------------------
class AppleBootDependencyGraph {
public:
    AppleBootDependencyGraph();
    ~AppleBootDependencyGraph() = default;

    // Reset graph to initial pending states
    void reset();

    // Query node information
    [[nodiscard]] const BootDependencyNode* get_node(BootNodeId id) const noexcept;
    [[nodiscard]] BootNodeState get_state(BootNodeId id) const noexcept;
    [[nodiscard]] bool is_satisfied(BootNodeId id) const noexcept;
    [[nodiscard]] bool can_execute(BootNodeId id, std::string* out_reason = nullptr) const;

    // State transitions
    [[nodiscard]] bool mark_satisfied(BootNodeId id, std::uint64_t step_info = 0, std::string* out_error = nullptr);
    void mark_failed(BootNodeId id, const std::string& reason);

    // Topological sort and validation
    [[nodiscard]] bool get_topological_order(std::vector<BootNodeId>& out_order) const;
    [[nodiscard]] bool is_dag_valid() const;
    [[nodiscard]] std::size_t total_nodes() const noexcept { return nodes_.size(); }
    [[nodiscard]] std::size_t satisfied_count() const noexcept;

    // Reporting & Visualizations
    [[nodiscard]] std::string generate_mermaid_graph() const;
    [[nodiscard]] std::string generate_dependency_report() const;
    [[nodiscard]] static const char* node_to_string(BootNodeId id) noexcept;
    [[nodiscard]] static const char* state_to_string(BootNodeState state) noexcept;

private:
    void init_graph();

    std::map<BootNodeId, BootDependencyNode> nodes_;
};

} // namespace winios::runtime
