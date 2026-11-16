#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loader/macho_loader.h"

namespace winios::loader {

// Maximum allowed dependency graph traversal depth to prevent stack overflow
constexpr std::size_t kMaxDependencyDepth = 64;

// -----------------------------------------------------------------------------
// Mach-O Dependency Relationship
// -----------------------------------------------------------------------------
struct MachODependencyEdge {
    std::string   raw_path;
    std::string   resolved_path;
    bool          is_weak{false};
    bool          is_reexport{false};
    bool          is_upward{false};
    std::uint32_t current_version{0};
    std::uint32_t compatibility_version{0};
};

struct MachODependencyNode {
    std::string                     id;
    std::string                     install_name;
    std::string                     dylinker;
    std::vector<std::string>        rpaths;
    std::vector<MachODependencyEdge> dependencies;
    bool                            has_chained_fixups{false};
    bool                            has_dyld_info{false};
    bool                            has_main{false};
    std::uint64_t                   entry_offset{0};
};

// -----------------------------------------------------------------------------
// Dependency Graph Traversal Result
// -----------------------------------------------------------------------------
struct DependencyResolutionResult {
    bool                     success{false};
    bool                     has_cycle{false};
    bool                     depth_exceeded{false};
    std::vector<std::string> load_order;        // Topologically sorted load order (dependencies first)
    std::vector<std::string> cycle_nodes;
    std::vector<std::string> missing_required;
    std::vector<std::string> missing_weak;
    std::string              error_message;
};

// -----------------------------------------------------------------------------
// MachODependencyGraph: Robust Dependency Analyzer & RPATH Resolver
// -----------------------------------------------------------------------------
class MachODependencyGraph {
public:
    MachODependencyGraph() = default;
    ~MachODependencyGraph() = default;

    // Parse load commands from a Mach-O byte buffer and add node to the graph
    [[nodiscard]] bool add_binary(
        const std::uint8_t* data,
        std::size_t size,
        std::string_view node_id,
        std::string& out_error);

    // Add node directly from parsed MachOLoadResult
    bool add_macho_load_result(const MachOLoadResult& result, std::string_view node_id);

    // Register a pre-constructed node
    void add_node(MachODependencyNode node);

    // Query node
    [[nodiscard]] const MachODependencyNode* get_node(std::string_view node_id) const noexcept;

    // Clear nodes
    void clear() noexcept { nodes_.clear(); }

    // Count of nodes
    [[nodiscard]] std::size_t size() const noexcept { return nodes_.size(); }

    // Resolve @rpath, @executable_path, @loader_path
    [[nodiscard]] std::string resolve_path(
        std::string_view raw_path,
        std::string_view executable_path,
        const std::vector<std::string>& active_rpaths,
        std::string_view loader_path = "") const;

    // Expand all ordered @rpath fallback candidates
    [[nodiscard]] std::vector<std::string> resolve_rpath_fallbacks(
        std::string_view raw_path,
        std::string_view executable_path,
        const std::vector<std::string>& active_rpaths,
        std::string_view loader_path = "") const;

    // Perform topological dependency sort, cycle detection, and depth limit checking
    [[nodiscard]] DependencyResolutionResult resolve_dependencies(
        std::string_view root_node_id,
        const std::set<std::string>& available_libraries,
        std::size_t max_depth = kMaxDependencyDepth) const;

private:
    std::map<std::string, MachODependencyNode> nodes_;
};

} // namespace winios::loader
