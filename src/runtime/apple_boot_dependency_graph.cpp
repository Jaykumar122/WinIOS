#include "runtime/apple_boot_dependency_graph.h"

#include <queue>
#include <sstream>

namespace winios::runtime {

AppleBootDependencyGraph::AppleBootDependencyGraph() {
    init_graph();
}

void AppleBootDependencyGraph::reset() {
    init_graph();
}

void AppleBootDependencyGraph::init_graph() {
    nodes_.clear();

    auto add_node = [this](BootNodeId id, std::string name, std::string desc, std::vector<BootNodeId> deps) {
        BootDependencyNode node;
        node.id = id;
        node.name = std::move(name);
        node.description = std::move(desc);
        node.state = BootNodeState::Pending;
        node.dependencies = std::move(deps);
        nodes_[id] = std::move(node);
    };

    add_node(BootNodeId::HardwareProfile_Init,
             "HardwareProfile_Init",
             "iPhone 15 A16 Bionic architectural profile validated",
             {});

    add_node(BootNodeId::PhysicalMemory_Map,
             "PhysicalMemory_Map",
             "Physical RAM (base 0x40000000) and MMIO apertures configured for 16 KB pages",
             {BootNodeId::HardwareProfile_Init});

    add_node(BootNodeId::AIC_InterruptController_Init,
             "AIC_InterruptController_Init",
             "Apple AIC 2 interrupt controller registered and banked per-core",
             {BootNodeId::PhysicalMemory_Map});

    add_node(BootNodeId::DeviceTree_Ingestion,
             "DeviceTree_Ingestion",
             "Apple DeviceTree ADTS validated and staged in memory",
             {BootNodeId::HardwareProfile_Init, BootNodeId::PhysicalMemory_Map});

    add_node(BootNodeId::KernelCache_Admission,
             "KernelCache_Admission",
             "64-bit ARM64e Mach-O kernelcache admitted and integrity verified",
             {BootNodeId::HardwareProfile_Init});

    add_node(BootNodeId::BootArgs_Preparation,
             "BootArgs_Preparation",
             "Darwin struct boot_args Revision 2 constructed for register X0",
             {BootNodeId::PhysicalMemory_Map, BootNodeId::DeviceTree_Ingestion, BootNodeId::KernelCache_Admission});

    add_node(BootNodeId::Kernel_Entry_Execution,
             "Kernel_Entry_Execution",
             "Guest CPU begins execution at kernel entry point at EL1",
             {BootNodeId::BootArgs_Preparation, BootNodeId::AIC_InterruptController_Init});

    add_node(BootNodeId::XNU_Early_Init,
             "XNU_Early_Init",
             "High-half kernel MMU page tables armed and KASLR slide initialized",
             {BootNodeId::Kernel_Entry_Execution});

    add_node(BootNodeId::IOKit_Platform_Matching,
             "IOKit_Platform_Matching",
             "AppleARMPE Platform Expert probes device tree and matches drivers",
             {BootNodeId::XNU_Early_Init});

    add_node(BootNodeId::RootFS_Mount,
             "RootFS_Mount",
             "Ramdisk or APFS Sealed System Volume (SSV) attached and mounted to VFS",
             {BootNodeId::IOKit_Platform_Matching});

    add_node(BootNodeId::TrustCache_Load,
             "TrustCache_Load",
             "Cryptographic TrustCache loaded to admit system userspace binaries",
             {BootNodeId::RootFS_Mount});

    add_node(BootNodeId::Launchd_Spawn,
             "Launchd_Spawn",
             "Darwin PID 1 (/sbin/launchd) spawned into userspace at EL0",
             {BootNodeId::TrustCache_Load});

    add_node(BootNodeId::Userspace_Daemons,
             "Userspace_Daemons",
             "launchd spawns essential daemons (logd, notifyd, fseventsd)",
             {BootNodeId::Launchd_Spawn});

    add_node(BootNodeId::SpringBoard_Display,
             "SpringBoard_Display",
             "Genuine SpringBoard process initializes UI and renders native Home Screen",
             {BootNodeId::Userspace_Daemons, BootNodeId::IOKit_Platform_Matching});
}

const BootDependencyNode* AppleBootDependencyGraph::get_node(BootNodeId id) const noexcept {
    auto it = nodes_.find(id);
    return it != nodes_.end() ? &it->second : nullptr;
}

BootNodeState AppleBootDependencyGraph::get_state(BootNodeId id) const noexcept {
    const auto* node = get_node(id);
    return node ? node->state : BootNodeState::Pending;
}

bool AppleBootDependencyGraph::is_satisfied(BootNodeId id) const noexcept {
    return get_state(id) == BootNodeState::Satisfied;
}

bool AppleBootDependencyGraph::can_execute(BootNodeId id, std::string* out_reason) const {
    const auto* node = get_node(id);
    if (!node) {
        if (out_reason) *out_reason = "Node ID not found in dependency graph";
        return false;
    }

    if (node->state == BootNodeState::Satisfied) {
        return true; // Already executed
    }

    for (BootNodeId dep_id : node->dependencies) {
        const auto* dep = get_node(dep_id);
        if (!dep || dep->state != BootNodeState::Satisfied) {
            if (out_reason) {
                std::string dep_state_str = dep ? state_to_string(dep->state) : "Missing";
                *out_reason = "Prerequisite '" + std::string(node_to_string(dep_id)) +
                              "' is not satisfied (current state: " + dep_state_str + ")";
            }
            return false;
        }
    }

    return true;
}

bool AppleBootDependencyGraph::mark_satisfied(BootNodeId id, std::uint64_t step_info, std::string* out_error) {
    if (!can_execute(id, out_error)) {
        auto it = nodes_.find(id);
        if (it != nodes_.end()) {
            it->second.state = BootNodeState::Blocked;
        }
        return false;
    }

    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        it->second.state = BootNodeState::Satisfied;
        it->second.timestamp_or_step = step_info;
        it->second.failure_reason.clear();
        return true;
    }
    return false;
}

void AppleBootDependencyGraph::mark_failed(BootNodeId id, const std::string& reason) {
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        it->second.state = BootNodeState::Failed;
        it->second.failure_reason = reason;
    }

    // Cascade block state to all downstream dependents
    std::queue<BootNodeId> q;
    q.push(id);

    while (!q.empty()) {
        BootNodeId cur = q.front();
        q.pop();

        for (auto& [child_id, child_node] : nodes_) {
            for (BootNodeId dep : child_node.dependencies) {
                if (dep == cur) {
                    if (child_node.state != BootNodeState::Failed) {
                        child_node.state = BootNodeState::Blocked;
                        child_node.failure_reason = "Blocked by failure of prerequisite: " + std::string(node_to_string(cur));
                    }
                    q.push(child_id);
                }
            }
        }
    }
}

bool AppleBootDependencyGraph::get_topological_order(std::vector<BootNodeId>& out_order) const {
    out_order.clear();

    std::map<BootNodeId, std::size_t> in_degree;
    std::map<BootNodeId, std::vector<BootNodeId>> adj;

    for (const auto& [id, node] : nodes_) {
        in_degree[id] = node.dependencies.size();
        for (BootNodeId dep : node.dependencies) {
            adj[dep].push_back(id);
        }
    }

    std::queue<BootNodeId> q;
    for (const auto& [id, deg] : in_degree) {
        if (deg == 0) {
            q.push(id);
        }
    }

    while (!q.empty()) {
        BootNodeId u = q.front();
        q.pop();
        out_order.push_back(u);

        for (BootNodeId v : adj[u]) {
            if (--in_degree[v] == 0) {
                q.push(v);
            }
        }
    }

    return out_order.size() == nodes_.size();
}

bool AppleBootDependencyGraph::is_dag_valid() const {
    std::vector<BootNodeId> order;
    return get_topological_order(order) && !order.empty();
}

std::size_t AppleBootDependencyGraph::satisfied_count() const noexcept {
    std::size_t count = 0;
    for (const auto& [id, node] : nodes_) {
        if (node.state == BootNodeState::Satisfied) {
            count++;
        }
    }
    return count;
}

const char* AppleBootDependencyGraph::node_to_string(BootNodeId id) noexcept {
    switch (id) {
        case BootNodeId::HardwareProfile_Init:         return "HardwareProfile_Init";
        case BootNodeId::PhysicalMemory_Map:           return "PhysicalMemory_Map";
        case BootNodeId::AIC_InterruptController_Init: return "AIC_InterruptController_Init";
        case BootNodeId::DeviceTree_Ingestion:         return "DeviceTree_Ingestion";
        case BootNodeId::KernelCache_Admission:        return "KernelCache_Admission";
        case BootNodeId::BootArgs_Preparation:         return "BootArgs_Preparation";
        case BootNodeId::Kernel_Entry_Execution:       return "Kernel_Entry_Execution";
        case BootNodeId::XNU_Early_Init:               return "XNU_Early_Init";
        case BootNodeId::IOKit_Platform_Matching:      return "IOKit_Platform_Matching";
        case BootNodeId::RootFS_Mount:                 return "RootFS_Mount";
        case BootNodeId::TrustCache_Load:              return "TrustCache_Load";
        case BootNodeId::Launchd_Spawn:                return "Launchd_Spawn";
        case BootNodeId::Userspace_Daemons:            return "Userspace_Daemons";
        case BootNodeId::SpringBoard_Display:          return "SpringBoard_Display";
    }
    return "Unknown";
}

const char* AppleBootDependencyGraph::state_to_string(BootNodeState state) noexcept {
    switch (state) {
        case BootNodeState::Pending:   return "Pending";
        case BootNodeState::Satisfied: return "Satisfied";
        case BootNodeState::Blocked:   return "Blocked";
        case BootNodeState::Failed:    return "Failed";
    }
    return "Unknown";
}

std::string AppleBootDependencyGraph::generate_mermaid_graph() const {
    std::stringstream ss;
    ss << "```mermaid\n";
    ss << "flowchart TD\n";

    for (const auto& [id, node] : nodes_) {
        std::string label = node.name + " [" + state_to_string(node.state) + "]";
        ss << "    " << node_to_string(id) << "[\"" << label << "\"]\n";
    }

    for (const auto& [id, node] : nodes_) {
        for (BootNodeId dep : node.dependencies) {
            ss << "    " << node_to_string(dep) << " --> " << node_to_string(id) << "\n";
        }
    }

    ss << "```\n";
    return ss.str();
}

std::string AppleBootDependencyGraph::generate_dependency_report() const {
    std::stringstream ss;
    ss << "# iPhone 15 Native iOS Boot Dependency Execution Report\n\n";
    ss << "| Boot Stage Node | State | Dependencies | Step / Instruction Count | Description |\n";
    ss << "| :--- | :--- | :--- | :--- | :--- |\n";

    std::vector<BootNodeId> order;
    (void)get_topological_order(order);

    for (BootNodeId id : order) {
        const auto* node = get_node(id);
        if (!node) continue;

        std::string deps_str;
        if (node->dependencies.empty()) {
            deps_str = "(root)";
        } else {
            for (std::size_t i = 0; i < node->dependencies.size(); ++i) {
                if (i > 0) deps_str += ", ";
                deps_str += node_to_string(node->dependencies[i]);
            }
        }

        std::string state_fmt;
        if (node->state == BootNodeState::Satisfied) state_fmt = "**SATISFIED**";
        else if (node->state == BootNodeState::Failed) state_fmt = "**FAILED**";
        else if (node->state == BootNodeState::Blocked) state_fmt = "*BLOCKED*";
        else state_fmt = "Pending";

        ss << "| `" << node->name << "` | " << state_fmt << " | " << deps_str << " | "
           << node->timestamp_or_step << " | " << node->description << " |\n";
    }

    return ss.str();
}

} // namespace winios::runtime
