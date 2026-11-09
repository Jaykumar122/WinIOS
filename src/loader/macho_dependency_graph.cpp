#include "loader/macho_dependency_graph.h"

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
            case kCmdReexportDylibCustom: {
                if (cmd_hdr.cmdsize >= sizeof(DylibCommandRaw)) {
                    DylibCommandRaw dylib_cmd{};
                    std::memcpy(&dylib_cmd, cmd_data, sizeof(dylib_cmd));

                    MachODependencyEdge edge;
                    edge.raw_path = extract_lc_string(cmd_data, cmd_hdr.cmdsize, dylib_cmd.name_offset);
                    edge.resolved_path = edge.raw_path;
                    edge.is_weak = (cmd_hdr.cmd == kCmdLoadWeakDylib);
                    edge.is_reexport = (cmd_hdr.cmd == kCmdReexportDylibCustom);
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
    const std::vector<std::string>& active_rpaths) const {

    std::string path_str(raw_path);

    // 1. Resolve @executable_path
    const std::string kExecPrefix = "@executable_path/";
    if (path_str.rfind(kExecPrefix, 0) == 0) {
        std::string exec_dir = "/usr/bin";
        auto last_slash = executable_path.find_last_of('/');
        if (last_slash != std::string_view::npos) {
            exec_dir = std::string(executable_path.substr(0, last_slash));
        }
        return exec_dir + "/" + path_str.substr(kExecPrefix.size());
    }

    // 2. Resolve @rpath
    const std::string kRpathPrefix = "@rpath/";
    if (path_str.rfind(kRpathPrefix, 0) == 0) {
        std::string sub = path_str.substr(kRpathPrefix.size());
        if (!active_rpaths.empty()) {
            return active_rpaths.front() + "/" + sub;
        }
        return "/usr/lib/" + sub;
    }

    return path_str;
}

DependencyResolutionResult MachODependencyGraph::resolve_dependencies(
    std::string_view root_node_id,
    const std::set<std::string>& available_libraries) const {

    DependencyResolutionResult result;

    auto root = get_node(root_node_id);
    if (!root) {
        result.error_message = "Root node not found: " + std::string(root_node_id);
        return result;
    }

    // Colors: 0 = unvisited, 1 = visiting (grey), 2 = visited (black)
    std::map<std::string, int> visit_state;
    std::vector<std::string> post_order;

    auto dfs = [&](auto& self, const std::string& current_id) -> bool {
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
                }

                // Check cycle
                if (visit_state[target] == 1) {
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
        return true;
    };

    if (!dfs(dfs, std::string(root_node_id))) {
        result.success = false;
        result.error_message = "Dependency cycle detected in Mach-O dependency graph";
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

} // namespace winios::loader
