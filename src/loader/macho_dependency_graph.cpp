#include "loader/macho_dependency_graph.h"
#include "loader/macho_user_loader.h"

#include <algorithm>
#include <cstring>
#include "loader/userspace_macho_loader.h"

namespace winios::loader {

namespace {

// Additional load command definitions if not in header
constexpr std::uint32_t kCmdReexportDylibCustom = 0x8000001Fu; // LC_REEXPORT_DYLIB
constexpr std::uint32_t kCmdIdDylibCustom       = 0x0000000Du; // LC_ID_DYLIB
constexpr std::uint32_t kCmdDyldInfoCustom      = 0x00000022u; // LC_DYLD_INFO
constexpr std::uint32_t kCmdDyldInfoOnlyCustom  = 0x80000022u; // LC_DYLD_INFO_ONLY

#pragma pack(push, 1)
struct DylibCommandRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t name_offset{0};
    std::uint32_t timestamp{0};
    std::uint32_t current_version{0};
    std::uint32_t compatibility_version{0};
};

struct RpathCommandRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t path_offset{0};
};

struct DylinkerCommandRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t name_offset{0};
};

struct EntryPointCommandRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint64_t entryoff{0};
    std::uint64_t stacksize{0};
};
#pragma pack(pop)

// Helper to extract a null-terminated string from load command
std::string extract_lc_string(
    const std::uint8_t* cmd_bytes,
    std::size_t cmdsize,
    std::uint32_t str_offset) {

    if (str_offset >= cmdsize) return "";
    const char* str_start = reinterpret_cast<const char*>(cmd_bytes + str_offset);
    std::size_t max_len = cmdsize - str_offset;
    std::size_t len = 0;
    while (len < max_len && str_start[len] != '\0') {
        len++;
    }
    return std::string(str_start, len);
}

} // namespace

bool MachODependencyGraph::add_binary(
    const std::uint8_t* data,
    std::size_t size,
    std::string_view node_id,
    std::string& out_error) {

    if (!data || size < sizeof(MachOHeader64Raw)) {
        out_error = "Buffer smaller than 64-bit Mach-O header";
        return false;
    }

    MachOHeader64Raw hdr{};
    std::memcpy(&hdr, data, sizeof(hdr));

    if (hdr.magic != kMachO64Magic) {
        out_error = "Invalid Mach-O 64-bit magic (expected MH_MAGIC_64)";
        return false;
    }

    if (sizeof(MachOHeader64Raw) + hdr.sizeofcmds > size) {
        out_error = "sizeofcmds exceeds binary buffer size";
        return false;
    }

    MachODependencyNode node;
    node.id = std::string(node_id);

    std::size_t offset = sizeof(MachOHeader64Raw);
    for (std::uint32_t i = 0; i < hdr.ncmds; ++i) {
        if (offset + sizeof(MachOLoadCommandRaw) > size) {
            out_error = "Load command header truncated";
            return false;
        }

        MachOLoadCommandRaw cmd_hdr{};
        std::memcpy(&cmd_hdr, data + offset, sizeof(cmd_hdr));

        if (cmd_hdr.cmdsize < sizeof(MachOLoadCommandRaw) || offset + cmd_hdr.cmdsize > size) {
            out_error = "Corrupted load command size at index " + std::to_string(i);
            return false;
        }

        const std::uint8_t* cmd_data = data + offset;

        switch (cmd_hdr.cmd) {
            case kCmdLoadDylib:
            case kCmdLoadWeakDylib:
            case kCmdReexportDylibCustom:
            case kLoadCmdLoadUpwardDylib: {
                if (cmd_hdr.cmdsize >= sizeof(DylibCommandRaw)) {
                    DylibCommandRaw dylib_cmd{};
                    std::memcpy(&dylib_cmd, cmd_data, sizeof(dylib_cmd));

                    MachODependencyEdge edge;
                    edge.raw_path = extract_lc_string(cmd_data, cmd_hdr.cmdsize, dylib_cmd.name_offset);
                    edge.resolved_path = edge.raw_path;
                    edge.is_weak = (cmd_hdr.cmd == kCmdLoadWeakDylib);
                    edge.is_reexport = (cmd_hdr.cmd == kCmdReexportDylibCustom);
                    edge.is_upward = (cmd_hdr.cmd == kLoadCmdLoadUpwardDylib);
                    edge.current_version = dylib_cmd.current_version;
                    edge.compatibility_version = dylib_cmd.compatibility_version;

                    if (!edge.raw_path.empty()) {
                        node.dependencies.push_back(std::move(edge));
                    }
                }
                break;
            }

            case kCmdIdDylibCustom: {
                if (cmd_hdr.cmdsize >= sizeof(DylibCommandRaw)) {
                    DylibCommandRaw id_cmd{};
                    std::memcpy(&id_cmd, cmd_data, sizeof(id_cmd));
                    node.install_name = extract_lc_string(cmd_data, cmd_hdr.cmdsize, id_cmd.name_offset);
                }
                break;
            }

            case kCmdRpath: {
                if (cmd_hdr.cmdsize >= sizeof(RpathCommandRaw)) {
                    RpathCommandRaw rp{};
                    std::memcpy(&rp, cmd_data, sizeof(rp));
                    std::string rpath = extract_lc_string(cmd_data, cmd_hdr.cmdsize, rp.path_offset);
                    if (!rpath.empty()) {
                        node.rpaths.push_back(std::move(rpath));
                    }
                }
                break;
            }

            case kCmdLoadDylinker: {
                if (cmd_hdr.cmdsize >= sizeof(DylinkerCommandRaw)) {
                    DylinkerCommandRaw dl{};
                    std::memcpy(&dl, cmd_data, sizeof(dl));
                    node.dylinker = extract_lc_string(cmd_data, cmd_hdr.cmdsize, dl.name_offset);
                }
                break;
            }

            case kCmdMain: {
                if (cmd_hdr.cmdsize >= sizeof(EntryPointCommandRaw)) {
                    EntryPointCommandRaw ep{};
                    std::memcpy(&ep, cmd_data, sizeof(ep));
                    node.has_main = true;
                    node.entry_offset = ep.entryoff;
                }
                break;
            }

            case kCmdDyldChainedFixups: {
                node.has_chained_fixups = true;
                break;
            }

            case kCmdDyldInfoCustom:
            case kCmdDyldInfoOnlyCustom: {
                node.has_dyld_info = true;
                break;
            }

            default:
                break;
        }

        offset += cmd_hdr.cmdsize;
    }

    nodes_[node.id] = std::move(node);
    return true;
}

bool MachODependencyGraph::add_macho_load_result(const MachOLoadResult& result, std::string_view node_id) {
    MachODependencyNode node;
    node.id = std::string(node_id);
    node.install_name = result.dylib_id;
    node.rpaths = result.rpaths;
    node.has_main = (result.entry_point != 0);
    node.entry_offset = result.entry_point;

    for (const auto& dep : result.dylib_dependencies) {
        MachODependencyEdge edge;
        edge.raw_path = dep.path;
        edge.resolved_path = dep.path;
        edge.is_weak = dep.is_weak;
        edge.is_reexport = dep.is_reexport;
        edge.is_upward = dep.is_upward;
        edge.current_version = dep.current_version;
        edge.compatibility_version = dep.compatibility_version;
        node.dependencies.push_back(std::move(edge));
    }

    nodes_[node.id] = std::move(node);
    return true;
}

void MachODependencyGraph::add_node(MachODependencyNode node) {
    nodes_[node.id] = std::move(node);
}

const MachODependencyNode* MachODependencyGraph::get_node(std::string_view node_id) const noexcept {
    auto it = nodes_.find(std::string(node_id));
    if (it != nodes_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string MachODependencyGraph::resolve_path(
    std::string_view raw_path,
    std::string_view executable_path,
    const std::vector<std::string>& active_rpaths,
    std::string_view loader_path) const {

    auto candidates = resolve_rpath_fallbacks(raw_path, executable_path, active_rpaths, loader_path);
    if (!candidates.empty()) {
        return candidates.front();
    }
    return std::string(raw_path);
}

std::vector<std::string> MachODependencyGraph::resolve_rpath_fallbacks(
    std::string_view raw_path,
    std::string_view executable_path,
    const std::vector<std::string>& active_rpaths,
    std::string_view loader_path) const {

    std::string path_str(raw_path);
    std::vector<std::string> results;

    std::string exec_dir = "/usr/bin";
    auto last_slash_exec = executable_path.find_last_of('/');
    if (last_slash_exec != std::string_view::npos) {
        exec_dir = std::string(executable_path.substr(0, last_slash_exec));
    }

    std::string loader_dir = exec_dir;
    if (!loader_path.empty()) {
        auto last_slash_loader = loader_path.find_last_of('/');
        if (last_slash_loader != std::string_view::npos) {
            loader_dir = std::string(loader_path.substr(0, last_slash_loader));
        } else {
            loader_dir = std::string(loader_path);
        }
    }

    // 1. Resolve @executable_path
    const std::string kExecPrefix = "@executable_path/";
    if (path_str.rfind(kExecPrefix, 0) == 0) {
        results.push_back(exec_dir + "/" + path_str.substr(kExecPrefix.size()));
        return results;
    }

    // 2. Resolve @loader_path
    const std::string kLoaderPrefix = "@loader_path/";
    if (path_str.rfind(kLoaderPrefix, 0) == 0) {
        results.push_back(loader_dir + "/" + path_str.substr(kLoaderPrefix.size()));
        return results;
    }

    // 3. Resolve @rpath
    const std::string kRpathPrefix = "@rpath/";
    if (path_str.rfind(kRpathPrefix, 0) == 0) {
        std::string sub = path_str.substr(kRpathPrefix.size());
        for (const auto& rp : active_rpaths) {
            std::string candidate_dir = rp;
            if (candidate_dir.rfind(kExecPrefix, 0) == 0) {
                candidate_dir = exec_dir + "/" + candidate_dir.substr(kExecPrefix.size());
            } else if (candidate_dir.rfind(kLoaderPrefix, 0) == 0) {
                candidate_dir = loader_dir + "/" + candidate_dir.substr(kLoaderPrefix.size());
            }
            if (!candidate_dir.empty() && candidate_dir.back() == '/') {
                candidate_dir.pop_back();
            }
            results.push_back(candidate_dir + "/" + sub);
        }
        results.push_back("/usr/lib/" + sub);
        return results;
    }

    results.push_back(path_str);
    return results;
}

DependencyResolutionResult MachODependencyGraph::resolve_dependencies(
    std::string_view root_node_id,
    const std::set<std::string>& available_libraries,
    std::size_t max_depth) const {

    DependencyResolutionResult result;

    auto root = get_node(root_node_id);
    if (!root) {
        result.error_message = "Root node not found: " + std::string(root_node_id);
        return result;
    }

    // Colors: 0 = unvisited, 1 = visiting (grey), 2 = visited (black)
    std::map<std::string, int> visit_state;
    std::vector<std::string> post_order;
    std::size_t current_depth = 0;

    auto dfs = [&](auto& self, const std::string& current_id) -> bool {
        if (current_depth >= max_depth) {
            result.depth_exceeded = true;
            result.error_message = "Dependency graph traversal exceeded maximum depth limit (" + std::to_string(max_depth) + ")";
            return false;
        }

        current_depth++;
        visit_state[current_id] = 1;

        auto node = get_node(current_id);
        if (node) {
            for (const auto& edge : node->dependencies) {
                std::string target = edge.resolved_path;

                // Check missing libraries
                if (available_libraries.find(target) == available_libraries.end() &&
                    nodes_.find(target) == nodes_.end()) {
                    if (edge.is_weak) {
                        result.missing_weak.push_back(target);
                    } else {
                        result.missing_required.push_back(target);
                    }
                    continue;
                }

                // Upward dependencies are allowed to point back to an ancestor without creating a cycle error
                if (visit_state[target] == 1) {
                    if (edge.is_upward) {
                        continue;
                    }
                    result.has_cycle = true;
                    result.cycle_nodes.push_back(current_id);
                    result.cycle_nodes.push_back(target);
                    return false;
                }

                if (visit_state[target] == 0) {
                    if (!self(self, target)) {
                        return false;
                    }
                }
            }
        }

        visit_state[current_id] = 2;
        post_order.push_back(current_id);
        current_depth--;
        return true;
    };

    if (!dfs(dfs, std::string(root_node_id))) {
        result.success = false;
        if (!result.depth_exceeded && !result.has_cycle) {
            result.error_message = "Dependency resolution failed";
        } else if (result.has_cycle && result.error_message.empty()) {
            result.error_message = "Dependency cycle detected in Mach-O dependency graph";
        }
        return result;
    }

    // Dependencies first (topological order)
    result.load_order = std::move(post_order);
    result.success = result.missing_required.empty();
    if (!result.success) {
        result.error_message = "Missing required dynamic library dependencies";
    }

    return result;
}

// =============================================================================
// MachoDependencyGraph (Campaign 212 Implementation)
// =============================================================================

inline constexpr std::uint32_t kLcLoadDylib212     = 0x0000000Cu;
inline constexpr std::uint32_t kLcIdDylib212       = 0x0000000Du;
inline constexpr std::uint32_t kLcLoadWeakDylib212 = 0x80000018u;
inline constexpr std::uint32_t kLcReexportDylib212 = 0x8000001Fu;

bool MachoDependencyGraph::parse_dependencies(
    const std::vector<std::uint8_t>& binary,
    std::vector<DylibDependency>& out_deps,
    std::string* out_id,
    std::string* out_err) {
    out_deps.clear();
    if (out_id) out_id->clear();

    if (!MachoUserLoader::validate_header(binary.data(), binary.size(), out_err)) {
        return false;
    }

    std::uint32_t ncmds = 0;
    std::uint32_t sizeofcmds = 0;
    std::memcpy(&ncmds, binary.data() + 16, sizeof(ncmds));
    std::memcpy(&sizeofcmds, binary.data() + 20, sizeof(sizeofcmds));

    if (32 + sizeofcmds > binary.size()) {
        if (out_err) *out_err = "Mach-O load command block exceeds binary size";
        return false;
    }

    std::size_t offset = 32;

    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (offset + 8 > binary.size()) {
            if (out_err) *out_err = "Truncated load command header";
            return false;
        }

        std::uint32_t cmd = 0;
        std::uint32_t cmdsize = 0;
        std::memcpy(&cmd, binary.data() + offset, sizeof(cmd));
        std::memcpy(&cmdsize, binary.data() + offset + 4, sizeof(cmdsize));

        if (cmdsize < 8 || offset + cmdsize > binary.size()) {
            if (out_err) *out_err = "Invalid load command size";
            return false;
        }

        if (cmd == kLcIdDylib212) {
            if (cmdsize >= 24) {
                std::uint32_t name_offset = 0;
                std::memcpy(&name_offset, binary.data() + offset + 8, sizeof(name_offset));
                if (name_offset < cmdsize && offset + name_offset < binary.size()) {
                    const char* str_start = reinterpret_cast<const char*>(binary.data() + offset + name_offset);
                    std::size_t max_len = cmdsize - name_offset;
                    std::size_t len = 0;
                    while (len < max_len && str_start[len] != '\0') ++len;
                    if (out_id) *out_id = std::string(str_start, len);
                }
            }
        } else if (cmd == kLcLoadDylib212 || cmd == kLcLoadWeakDylib212 || cmd == kLcReexportDylib212) {
            if (cmdsize >= 24) {
                std::uint32_t name_offset = 0;
                std::uint32_t cur_ver = 0;
                std::uint32_t compat_ver = 0;
                std::memcpy(&name_offset, binary.data() + offset + 8, sizeof(name_offset));
                std::memcpy(&cur_ver, binary.data() + offset + 16, sizeof(cur_ver));
                std::memcpy(&compat_ver, binary.data() + offset + 20, sizeof(compat_ver));

                if (name_offset < cmdsize && offset + name_offset < binary.size()) {
                    const char* str_start = reinterpret_cast<const char*>(binary.data() + offset + name_offset);
                    std::size_t max_len = cmdsize - name_offset;
                    std::size_t len = 0;
                    while (len < max_len && str_start[len] != '\0') ++len;

                    DylibDependency dep;
                    dep.raw_path = std::string(str_start, len);
                    dep.current_version = cur_ver;
                    dep.compatibility_version = compat_ver;
                    dep.is_weak = (cmd == kLcLoadWeakDylib212);
                    dep.is_reexport = (cmd == kLcReexportDylib212);

                    auto slash = dep.raw_path.find_last_of('/');
                    if (slash != std::string::npos) {
                        dep.resolved_name = dep.raw_path.substr(slash + 1);
                    } else {
                        dep.resolved_name = dep.raw_path;
                    }

                    out_deps.push_back(std::move(dep));
                }
            }
        }

        offset += cmdsize;
    }

    return true;
}

bool MachoDependencyGraph::add_image(
    const std::string& name,
    const std::vector<std::uint8_t>& binary,
    std::string* out_err) {
    if (nodes_.size() >= kMaxDependencies) {
        if (out_err) *out_err = "Maximum dependency count exceeded (" + std::to_string(kMaxDependencies) + ")";
        return false;
    }

    if (nodes_.find(name) != nodes_.end()) {
        if (out_err) *out_err = "Duplicate image name: " + name;
        return false;
    }

    std::vector<DylibDependency> deps;
    std::string dylib_id;
    if (!parse_dependencies(binary, deps, &dylib_id, out_err)) {
        return false;
    }

    DependencyNode node;
    node.name = name;
    node.dylib_id = dylib_id;
    node.binary_data = binary;
    node.dependencies = std::move(deps);

    nodes_[name] = std::move(node);
    return true;
}

bool MachoDependencyGraph::resolve_load_order(
    const std::string& root_name,
    std::vector<std::string>& out_load_order,
    std::string* out_err) const {
    out_load_order.clear();

    auto it = nodes_.find(root_name);
    if (it == nodes_.end()) {
        if (out_err) *out_err = "Root binary '" + root_name + "' not found in dependency graph";
        return false;
    }

    std::vector<std::string> visiting;
    std::vector<std::string> visited;
    std::vector<std::string> order;

    if (!dfs_resolve(root_name, 0, visiting, visited, order, out_err)) {
        return false;
    }

    out_load_order = std::move(order);
    return true;
}

bool MachoDependencyGraph::dfs_resolve(
    const std::string& current,
    std::size_t depth,
    std::vector<std::string>& visiting,
    std::vector<std::string>& visited,
    std::vector<std::string>& order,
    std::string* out_err) const {
    if (depth >= kMaxDepth) {
        if (out_err) *out_err = "Dependency traversal depth exceeded (" + std::to_string(kMaxDepth) + ")";
        return false;
    }

    if (std::find(visiting.begin(), visiting.end(), current) != visiting.end()) {
        if (out_err) *out_err = "Dependency cycle detected involving '" + current + "'";
        return false;
    }

    visiting.push_back(current);

    auto node_it = nodes_.find(current);
    if (node_it == nodes_.end()) {
        if (out_err) *out_err = "Node '" + current + "' not found";
        return false;
    }

    for (const auto& dep : node_it->second.dependencies) {
        std::string target_key;
        if (nodes_.find(dep.resolved_name) != nodes_.end()) {
            target_key = dep.resolved_name;
        } else if (nodes_.find(dep.raw_path) != nodes_.end()) {
            target_key = dep.raw_path;
        } else {
            for (const auto& [k, node] : nodes_) {
                if (node.dylib_id == dep.raw_path || node.dylib_id == dep.resolved_name || node.name == dep.resolved_name) {
                    target_key = k;
                    break;
                }
            }
        }

        if (target_key.empty()) {
            if (dep.is_weak) {
                continue;
            }
            if (out_err) *out_err = "Missing required dynamic library: '" + dep.raw_path + "'";
            return false;
        }

        if (target_key == current) {
            continue;
        }

        if (std::find(visited.begin(), visited.end(), target_key) != visited.end()) {
            continue;
        }

        if (!dfs_resolve(target_key, depth + 1, visiting, visited, order, out_err)) {
            return false;
        }
    }

    visiting.pop_back();
    visited.push_back(current);
    order.push_back(current);

    return true;
}

} // namespace winios::loader
