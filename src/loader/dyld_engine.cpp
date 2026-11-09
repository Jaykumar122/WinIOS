#include "loader/dyld_engine.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

namespace winios::loader {

const char* dyld_link_status_string(DyldLinkStatus s) noexcept {
    switch (s) {
        case DyldLinkStatus::Success:              return "Success";
        case DyldLinkStatus::DylibNotFound:        return "DylibNotFound";
        case DyldLinkStatus::UndefinedSymbol:      return "UndefinedSymbol";
        case DyldLinkStatus::InvalidChainedFixups: return "InvalidChainedFixups";
        case DyldLinkStatus::SharedCacheCorrupt:   return "SharedCacheCorrupt";
        case DyldLinkStatus::MemoryPatchFailed:    return "MemoryPatchFailed";
        case DyldLinkStatus::RelocationOverflow:   return "RelocationOverflow";
        case DyldLinkStatus::MalformedExportTrie:  return "MalformedExportTrie";
        case DyldLinkStatus::MalformedSymtab:      return "MalformedSymtab";
        default:                                   return "Unknown";
    }
}

DyldEngine::DyldEngine() {
    register_builtin_system_libraries();
}

void DyldEngine::register_builtin_system_libraries() {
    // 1. /usr/lib/libSystem.B.dylib (Essential Darwin runtime entry points)
    DyldLoadedModule libsystem;
    libsystem.name = "libSystem.B.dylib";
    libsystem.install_name = "/usr/lib/libSystem.B.dylib";
    libsystem.base_address = 0x0000000180000000ULL;
    libsystem.slide = 0;
    libsystem.exports["_exit"]               = 0x0000000180001000ULL;
    libsystem.exports["_printf"]             = 0x0000000180001040ULL;
    libsystem.exports["_mach_msg_trap"]      = 0x0000000180001080ULL;
    libsystem.exports["_mach_task_self"]     = 0x00000001800010C0ULL;
    libsystem.exports["_bootstrap_look_up"]  = 0x0000000180001100ULL;
    libsystem.exports["_bootstrap_register"] = 0x0000000180001140ULL;
    libsystem.exports["_dispatch_main"]      = 0x0000000180001180ULL;
    libsystem.exports["_malloc"]             = 0x00000001800011C0ULL;
    libsystem.exports["_free"]               = 0x0000000180001200ULL;
    libsystem.exports["_write"]              = 0x0000000180001240ULL;
    libsystem.exports["_read"]               = 0x0000000180001280ULL;
    libsystem.exports["_getpid"]             = 0x00000001800012C0ULL;
    libsystem.exports["___stack_chk_fail"]   = 0x0000000180001300ULL;
    libsystem.exports["___stack_chk_guard"]  = 0x0000000180001340ULL;
    libsystem.exports["dyld_stub_binder"]    = 0x0000000180001380ULL;
    libsystem.exports["__tlv_bootstrap"]     = 0x0000000180001400ULL;
    libsystem.exports["___tlv_bootstrap"]    = 0x0000000180001400ULL;
    register_module(std::move(libsystem));

    // 2. CoreFoundation Framework
    DyldLoadedModule corefoundation;
    corefoundation.name = "CoreFoundation";
    corefoundation.install_name = "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation";
    corefoundation.base_address = 0x0000000190000000ULL;
    corefoundation.slide = 0;
    corefoundation.exports["_CFRunLoopRun"]        = 0x0000000190001000ULL;
    corefoundation.exports["_CFRunLoopStop"]       = 0x0000000190001040ULL;
    corefoundation.exports["_CFRelease"]           = 0x0000000190001080ULL;
    corefoundation.exports["_CFStringCreateCopy"]  = 0x00000001900010C0ULL;
    register_module(std::move(corefoundation));
}

void DyldEngine::register_module(DyldLoadedModule module) {
    // Remove if module with same install_name or name exists
    auto it = std::remove_if(modules_.begin(), modules_.end(), [&](const DyldLoadedModule& m) {
        return m.install_name == module.install_name || m.name == module.name;
    });
    modules_.erase(it, modules_.end());
    modules_.push_back(std::move(module));
}

void DyldEngine::clear_modules() noexcept {
    modules_.clear();
}

std::uint32_t DyldEngine::get_module_ordinal(std::string_view name) const noexcept {
    for (std::size_t i = 0; i < modules_.size(); ++i) {
        if (modules_[i].name == name || modules_[i].install_name == name) {
            return static_cast<std::uint32_t>(i + 1);
        }
    }
    return 0;
}

bool DyldEngine::has_module(std::string_view name) const noexcept {
    return find_module(name) != nullptr;
}

const DyldLoadedModule* DyldEngine::find_module(std::string_view name) const noexcept {
    for (const auto& m : modules_) {
        if (m.name == name || m.install_name == name) {
            return &m;
        }
    }
    return nullptr;
}

std::optional<std::uint64_t> DyldEngine::resolve_symbol(
    std::string_view name,
    std::uint32_t ordinal) const noexcept {

    // Ordinal-specific resolution (1-based ordinal pointing to dependent dylib)
    if (ordinal > 0 && ordinal <= modules_.size()) {
        const auto& mod = modules_[ordinal - 1];
        auto it = mod.exports.find(std::string(name));
        if (it != mod.exports.end()) {
            return it->second + mod.slide;
        }
    }

    // Broad search across all registered modules
    for (const auto& mod : modules_) {
        auto it = mod.exports.find(std::string(name));
        if (it != mod.exports.end()) {
            return it->second + mod.slide;
        }
    }

    return std::nullopt;
}

DyldLinkStatus DyldEngine::resolve_symbol_two_level(
    std::string_view name,
    std::int32_t library_ordinal,
    bool is_weak_import,
    std::optional<std::uint64_t>& out_address,
    std::string& out_error) const noexcept {

    out_address.reset();

    // 1. Ordinal-specific lookup (1-based ordinal pointing to dependent dylib)
    if (library_ordinal > 0) {
        std::size_t idx = static_cast<std::size_t>(library_ordinal - 1);
        if (idx < modules_.size()) {
            const auto& mod = modules_[idx];
            auto it = mod.exports.find(std::string(name));
            if (it != mod.exports.end()) {
                out_address = it->second + mod.slide;
                return DyldLinkStatus::Success;
            }
        }
        if (is_weak_import) {
            out_address = 0;
            return DyldLinkStatus::Success;
        }
        out_error = "Symbol '" + std::string(name) + "' not found in designated library ordinal " + std::to_string(library_ordinal);
        return DyldLinkStatus::UndefinedSymbol;
    }

    // 2. BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE (-1)
    if (library_ordinal == BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE) {
        if (!modules_.empty()) {
            const auto& mod = modules_.front();
            auto it = mod.exports.find(std::string(name));
            if (it != mod.exports.end()) {
                out_address = it->second + mod.slide;
                return DyldLinkStatus::Success;
            }
        }
        if (is_weak_import) {
            out_address = 0;
            return DyldLinkStatus::Success;
        }
        out_error = "Symbol '" + std::string(name) + "' not found in main executable";
        return DyldLinkStatus::UndefinedSymbol;
    }

    // 3. Flat / broad lookup (BIND_SPECIAL_DYLIB_FLAT_LOOKUP or default 0)
    for (const auto& mod : modules_) {
        auto it = mod.exports.find(std::string(name));
        if (it != mod.exports.end()) {
            out_address = it->second + mod.slide;
            return DyldLinkStatus::Success;
        }
    }

    if (is_weak_import) {
        out_address = 0;
        return DyldLinkStatus::Success;
    }

    out_error = "Undefined symbol: " + std::string(name);
    return DyldLinkStatus::UndefinedSymbol;
}

namespace {

inline std::uint64_t read_uleb128(const std::uint8_t*& p, const std::uint8_t* end) noexcept {
    std::uint64_t result = 0;
    int shift = 0;
    while (p < end) {
        std::uint8_t byte = *p++;
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        if (shift >= 64) {
            break;
        }
    }
    return result;
}

inline void write_uleb128(std::vector<std::uint8_t>& buf, std::uint64_t val) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(val & 0x7F);
        val >>= 7;
        if (val != 0) {
            byte |= 0x80;
        }
        buf.push_back(byte);
    } while (val != 0);
}

inline std::int64_t read_sleb128(const std::uint8_t*& p, const std::uint8_t* end) noexcept {
    std::int64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    while (p < end) {
        byte = *p++;
        result |= static_cast<std::int64_t>(byte & 0x7Fu) << shift;
        shift += 7;
        if ((byte & 0x80u) == 0) {
            break;
        }
        if (shift >= 64) {
            break;
        }
    }
    if ((shift < 64) && ((byte & 0x40u) != 0)) {
        result |= -(1LL << shift);
    }
    return result;
}

} // anonymous namespace

bool DyldEngine::parse_export_trie(
    const void* data,
    std::size_t size,
    std::vector<DyldExportedSymbolInfo>& out_symbols,
    std::string& out_error) {

    out_symbols.clear();
    if (!data || size == 0) {
        out_error = "Export trie data empty or null";
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto* end = bytes + size;

    std::set<std::size_t> visited_offsets;

    auto walk_node = [&](auto& self, std::size_t offset, const std::string& prefix) -> bool {
        if (offset >= size) {
            out_error = "Export trie node offset " + std::to_string(offset) + " exceeds trie size";
            return false;
        }
        if (!visited_offsets.insert(offset).second) {
            out_error = "Export trie cycle detected at offset " + std::to_string(offset);
            return false;
        }

        const std::uint8_t* p = bytes + offset;
        std::uint64_t terminal_size = read_uleb128(p, end);

        if (terminal_size > 0) {
            const std::uint8_t* term_p = p;
            if (term_p + terminal_size > end) {
                out_error = "Export trie terminal payload extends past trie bounds";
                return false;
            }

            DyldExportedSymbolInfo info;
            info.name = prefix;
            info.flags = static_cast<std::uint32_t>(read_uleb128(term_p, end));
            info.is_weak_definition = ((info.flags & EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION) != 0);

            if ((info.flags & EXPORT_SYMBOL_FLAGS_REEXPORT) != 0) {
                info.is_reexport = true;
                info.other_ordinal = static_cast<std::uint32_t>(read_uleb128(term_p, end));
                std::string reexport_name;
                while (term_p < end && *term_p != '\0') {
                    reexport_name.push_back(static_cast<char>(*term_p++));
                }
                if (term_p < end && *term_p == '\0') {
                    term_p++;
                }
                info.reexport_name = reexport_name.empty() ? prefix : reexport_name;
            } else if ((info.flags & EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER) != 0) {
                info.address = read_uleb128(term_p, end);
                std::uint64_t resolver_off = read_uleb128(term_p, end);
                (void)resolver_off;
            } else {
                info.address = read_uleb128(term_p, end);
            }

            out_symbols.push_back(std::move(info));
            p += terminal_size;
        }

        if (p >= end) {
            return true;
        }

        std::uint8_t child_count = *p++;
        for (std::uint8_t i = 0; i < child_count; ++i) {
            std::string edge;
            while (p < end && *p != '\0') {
                edge.push_back(static_cast<char>(*p++));
            }
            if (p < end && *p == '\0') {
                p++;
            }
            if (p >= end) {
                out_error = "Truncated child edge in export trie";
                return false;
            }
            std::uint64_t child_offset = read_uleb128(p, end);
            if (!self(self, static_cast<std::size_t>(child_offset), prefix + edge)) {
                return false;
            }
        }

        return true;
    };

    return walk_node(walk_node, 0, "");
}

bool DyldEngine::parse_symbol_table(
    const void* macho_data,
    std::size_t macho_size,
    std::uint32_t symoff,
    std::uint32_t nsyms,
    std::uint32_t stroff,
    std::uint32_t strsize,
    std::vector<DyldSymtabSymbolInfo>& out_symbols,
    std::string& out_error) {

    out_symbols.clear();
    if (!macho_data || macho_size == 0) {
        out_error = "Mach-O image buffer is null or empty";
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(macho_data);

    std::size_t symtab_bytes = static_cast<std::size_t>(nsyms) * 16ULL;
    if (static_cast<std::size_t>(symoff) + symtab_bytes > macho_size) {
        out_error = "Symbol table extends beyond Mach-O image bounds";
        return false;
    }

    if (static_cast<std::size_t>(stroff) + static_cast<std::size_t>(strsize) > macho_size) {
        out_error = "String table extends beyond Mach-O image bounds";
        return false;
    }

    const char* strtab = reinterpret_cast<const char*>(bytes + stroff);

    #pragma pack(push, 1)
    struct NList64Raw {
        std::uint32_t n_strx;
        std::uint8_t  n_type;
        std::uint8_t  n_sect;
        std::uint16_t n_desc;
        std::uint64_t n_value;
    };
    #pragma pack(pop)

    const auto* nlists = reinterpret_cast<const NList64Raw*>(bytes + symoff);

    for (std::uint32_t i = 0; i < nsyms; ++i) {
        const auto& nl = nlists[i];
        DyldSymtabSymbolInfo sym;
        sym.type = nl.n_type;
        sym.sect = nl.n_sect;
        sym.desc = nl.n_desc;
        sym.address = nl.n_value;

        constexpr std::uint8_t N_EXT = 0x01;
        constexpr std::uint16_t N_WEAK_DEF = 0x0080;
        constexpr std::uint16_t N_WEAK_REF = 0x0040;

        sym.is_external = ((nl.n_type & N_EXT) != 0);
        sym.is_weak_def = ((nl.n_desc & N_WEAK_DEF) != 0);
        sym.is_weak_ref = ((nl.n_desc & N_WEAK_REF) != 0);

        if (nl.n_strx < strsize) {
            sym.name = std::string(strtab + nl.n_strx);
        }

        out_symbols.push_back(std::move(sym));
    }

    return true;
}

bool DyldEngine::parse_binding_stream(
    const void* data,
    std::size_t size,
    const std::vector<std::uint64_t>& segment_vmaddrs,
    std::vector<DyldBindingRecord>& out_bindings,
    std::string& out_error) {

    if (!data || size == 0) {
        return true;
    }

    const auto* p = static_cast<const std::uint8_t*>(data);
    const auto* end = p + size;

    std::uint32_t seg_index = 0;
    std::uint64_t seg_offset = 0;
    std::string symbol_name;
    std::int64_t addend = 0;
    std::uint32_t dylib_ordinal = 0;
    bool is_weak = false;

    while (p < end) {
        std::uint8_t opcode_byte = *p++;
        std::uint8_t opcode = opcode_byte & 0xF0u;
        std::uint8_t imm = opcode_byte & 0x0Fu;

        switch (opcode) {
            case 0x00u: // BIND_OPCODE_DONE
                break;

            case 0x10u: // BIND_OPCODE_SET_DYLIB_ORDINAL_IMM
                dylib_ordinal = imm;
                break;

            case 0x20u: // BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB
                dylib_ordinal = static_cast<std::uint32_t>(read_uleb128(p, end));
                break;

            case 0x30u: // BIND_OPCODE_SET_DYLIB_SPECIAL_IMM
                if (imm == 0) {
                    dylib_ordinal = 0;
                } else {
                    dylib_ordinal = static_cast<std::uint32_t>(static_cast<std::int8_t>(0xF0u | imm));
                }
                break;

            case 0x40u: { // BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM
                is_weak = ((imm & 0x1u) != 0);
                symbol_name.clear();
                while (p < end && *p != '\0') {
                    symbol_name.push_back(static_cast<char>(*p++));
                }
                if (p < end && *p == '\0') {
                    p++;
                }
                break;
            }

            case 0x50u: // BIND_OPCODE_SET_TYPE_IMM
                // imm is bind type (1 = POINTER)
                break;

            case 0x60u: // BIND_OPCODE_SET_ADDEND_SLEB
                addend = read_sleb128(p, end);
                break;

            case 0x70u: // BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB
                seg_index = imm;
                seg_offset = read_uleb128(p, end);
                break;

            case 0x80u: // BIND_OPCODE_ADD_ADDR_ULEB
                seg_offset += read_uleb128(p, end);
                break;

            case 0x90u: { // BIND_OPCODE_DO_BIND
                std::uint64_t base = (seg_index < segment_vmaddrs.size()) ? segment_vmaddrs[seg_index] : 0;
                out_bindings.push_back(DyldBindingRecord{
                    symbol_name,
                    base + seg_offset,
                    addend,
                    dylib_ordinal,
                    is_weak
                });
                seg_offset += sizeof(void*);
                break;
            }

            case 0xA0u: { // BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB
                std::uint64_t base = (seg_index < segment_vmaddrs.size()) ? segment_vmaddrs[seg_index] : 0;
                out_bindings.push_back(DyldBindingRecord{
                    symbol_name,
                    base + seg_offset,
                    addend,
                    dylib_ordinal,
                    is_weak
                });
                seg_offset += sizeof(void*) + read_uleb128(p, end);
                break;
            }

            case 0xB0u: { // BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED
                std::uint64_t base = (seg_index < segment_vmaddrs.size()) ? segment_vmaddrs[seg_index] : 0;
                out_bindings.push_back(DyldBindingRecord{
                    symbol_name,
                    base + seg_offset,
                    addend,
                    dylib_ordinal,
                    is_weak
                });
                seg_offset += sizeof(void*) + (static_cast<std::uint64_t>(imm) * sizeof(void*));
                break;
            }

            case 0xC0u: { // BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB
                std::uint64_t count = read_uleb128(p, end);
                std::uint64_t skip = read_uleb128(p, end);
                std::uint64_t base = (seg_index < segment_vmaddrs.size()) ? segment_vmaddrs[seg_index] : 0;
                for (std::uint64_t i = 0; i < count; ++i) {
                    out_bindings.push_back(DyldBindingRecord{
                        symbol_name,
                        base + seg_offset,
                        addend,
                        dylib_ordinal,
                        is_weak
                    });
                    seg_offset += sizeof(void*) + skip;
                }
                break;
            }

            default:
                out_error = "Unknown dyld binding opcode: 0x" + std::to_string(opcode);
                return false;
        }
    }

    return true;
}

std::string DyldEngine::resolve_rpath_path(
    std::string_view install_name,
    const std::vector<std::string>& rpaths,
    std::string_view executable_dir,
    std::string_view loader_dir) {

    if (!install_name.starts_with("@rpath/")) {
        return std::string(install_name);
    }

    std::string_view suffix = install_name.substr(7);

    for (const auto& rp : rpaths) {
        std::string candidate_dir = rp;
        if (candidate_dir.starts_with("@executable_path")) {
            candidate_dir.replace(0, 16, executable_dir);
        } else if (candidate_dir.starts_with("@loader_path")) {
            candidate_dir.replace(0, 12, loader_dir);
        }

        std::string candidate_path = candidate_dir;
        if (!candidate_path.empty() && candidate_path.back() != '/' && candidate_path.back() != '\\') {
            candidate_path += '/';
        }
        candidate_path += std::string(suffix);

        std::ifstream check(candidate_path, std::ios::binary);
        if (check.good()) {
            return candidate_path;
        }
    }

    return "";
}

namespace {

inline std::size_t translate_patch_vaddr(
    std::uint64_t patch_vaddr,
    std::uint64_t base_vaddr,
    std::size_t mem_size) noexcept {

    if (patch_vaddr < mem_size) {
        return static_cast<std::size_t>(patch_vaddr);
    }
    if (base_vaddr > 0 && patch_vaddr >= base_vaddr) {
        std::uint64_t rel = patch_vaddr - base_vaddr;
        if (rel < mem_size) {
            return static_cast<std::size_t>(rel);
        }
    }
    return static_cast<std::size_t>(patch_vaddr % mem_size);
}

} // anonymous namespace

DyldLinkStatus DyldEngine::apply_rebases(
    Memory& memory,
    const std::vector<DyldRebaseRecord>& rebases,
    std::uint64_t slide,
    std::uint64_t base_vaddr,
    std::string& out_error) {

    if (slide == 0) {
        return DyldLinkStatus::Success; // No ASLR slide to apply
    }

    for (const auto& r : rebases) {
        std::size_t off = translate_patch_vaddr(r.patch_vaddr, base_vaddr, memory.size());
        if (off + 8 > memory.size()) {
            out_error = "Rebase patch offset exceeds memory size";
            return DyldLinkStatus::MemoryPatchFailed;
        }

        std::uint64_t current_val = memory.read_u64(off);
        std::uint64_t rebased_val = current_val + slide;
        memory.write_u64(off, rebased_val);
    }

    return DyldLinkStatus::Success;
}

DyldLinkStatus DyldEngine::apply_bindings(
    Memory& memory,
    const std::vector<DyldBindingRecord>& bindings,
    std::uint64_t base_vaddr,
    std::string& out_error) {

    for (const auto& b : bindings) {
        auto resolved = resolve_symbol(b.symbol_name, b.dylib_ordinal);

        if (!resolved.has_value()) {
            if (b.is_weak) {
                // Weak undefined symbol resolves to NULL
                std::size_t off = translate_patch_vaddr(b.patch_vaddr, base_vaddr, memory.size());
                if (off + 8 <= memory.size()) {
                    memory.write_u64(off, 0);
                }
                continue;
            }
            out_error = "Undefined dynamic symbol: " + b.symbol_name;
            return DyldLinkStatus::UndefinedSymbol;
        }

        std::uint64_t final_target = *resolved + static_cast<std::uint64_t>(b.addend);
        std::size_t off = translate_patch_vaddr(b.patch_vaddr, base_vaddr, memory.size());
        if (off + 8 > memory.size()) {
            out_error = "Binding patch offset exceeds memory bounds";
            return DyldLinkStatus::MemoryPatchFailed;
        }

        memory.write_u64(off, final_target);
    }

    return DyldLinkStatus::Success;
}

DyldLinkStatus DyldEngine::process_chained_fixups(
    const void* data,
    std::size_t size,
    Memory& memory,
    std::uint64_t base_vaddr,
    std::uint64_t slide,
    std::string& out_error) {

    if (!data || size < sizeof(DyldChainedFixupsHeaderRaw)) {
        out_error = "Chained fixups payload truncated";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const auto* hdr = reinterpret_cast<const DyldChainedFixupsHeaderRaw*>(bytes);
    if (hdr->fixups_version != 0 && hdr->fixups_version != 1) {
        out_error = "Unsupported chained fixups version";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    if (hdr->imports_offset + (static_cast<std::size_t>(hdr->imports_count) * sizeof(DyldChainedImportRaw)) > size) {
        out_error = "Chained fixups imports table out of bounds";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    if (hdr->symbols_offset > size) {
        out_error = "Chained fixups symbols pool out of bounds";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    if (hdr->starts_offset == 0) {
        return DyldLinkStatus::Success;
    }

    if (hdr->starts_offset + sizeof(DyldChainedStartsInImageRaw) > size) {
        out_error = "Chained fixups starts table extends beyond payload";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    const auto* starts_img = reinterpret_cast<const DyldChainedStartsInImageRaw*>(bytes + hdr->starts_offset);
    std::uint32_t seg_count = starts_img->seg_count;
    if (seg_count == 0) {
        return DyldLinkStatus::Success;
    }

    std::size_t seg_offsets_end = hdr->starts_offset + sizeof(std::uint32_t) + (static_cast<std::size_t>(seg_count) * sizeof(std::uint32_t));
    if (seg_offsets_end > size) {
        out_error = "Chained fixups segment offsets array out of bounds";
        return DyldLinkStatus::InvalidChainedFixups;
    }

    const auto* seg_info_offsets = reinterpret_cast<const std::uint32_t*>(bytes + hdr->starts_offset + sizeof(std::uint32_t));

    for (std::uint32_t seg_idx = 0; seg_idx < seg_count; ++seg_idx) {
        std::uint32_t seg_info_off = seg_info_offsets[seg_idx];
        if (seg_info_off == 0) {
            continue;
        }

        std::size_t seg_starts_off = static_cast<std::size_t>(hdr->starts_offset) + seg_info_off;
        if (seg_starts_off + sizeof(DyldChainedStartsInSegmentRaw) > size) {
            out_error = "Chained fixups segment header out of bounds";
            return DyldLinkStatus::InvalidChainedFixups;
        }

        const auto* seg = reinterpret_cast<const DyldChainedStartsInSegmentRaw*>(bytes + seg_starts_off);
        if (seg->page_size == 0 || seg->page_count == 0) {
            continue;
        }

        std::size_t page_starts_end = seg_starts_off + sizeof(DyldChainedStartsInSegmentRaw) + (static_cast<std::size_t>(seg->page_count) * sizeof(std::uint16_t));
        if (page_starts_end > size) {
            out_error = "Chained fixups page starts array out of bounds";
            return DyldLinkStatus::InvalidChainedFixups;
        }

        const auto* page_starts = reinterpret_cast<const std::uint16_t*>(bytes + seg_starts_off + sizeof(DyldChainedStartsInSegmentRaw));
        std::uint16_t format = seg->pointer_format;

        if (format != DYLD_CHAINED_PTR_ARM64E &&
            format != DYLD_CHAINED_PTR_64 &&
            format != DYLD_CHAINED_PTR_64_OFFSET &&
            format != DYLD_CHAINED_PTR_ARM64E_USERLAND &&
            format != DYLD_CHAINED_PTR_ARM64E_USERLAND24) {
            out_error = "Unsupported chained pointer format: " + std::to_string(format);
            return DyldLinkStatus::InvalidChainedFixups;
        }

        for (std::uint16_t p = 0; p < seg->page_count; ++p) {
            std::uint16_t pstart = page_starts[p];
            if (pstart == DYLD_CHAINED_PTR_START_NONE) {
                continue;
            }
            if (pstart & DYLD_CHAINED_PTR_START_MULTI) {
                pstart &= ~DYLD_CHAINED_PTR_START_MULTI;
            }

            std::uint64_t chain_loc = seg->segment_offset + (static_cast<std::uint64_t>(p) * seg->page_size) + pstart;
            std::set<std::uint64_t> visited_locations;

            while (true) {
                if (!visited_locations.insert(chain_loc).second) {
                    out_error = "Cyclic pointer chain detected at offset " + std::to_string(chain_loc);
                    return DyldLinkStatus::InvalidChainedFixups;
                }

                if (chain_loc + sizeof(std::uint64_t) > memory.size()) {
                    out_error = "Chained fixup offset exceeds memory bounds: " + std::to_string(chain_loc);
                    return DyldLinkStatus::MemoryPatchFailed;
                }

                std::uint64_t raw_val = memory.read_u64(chain_loc);
                std::uint64_t next_stride = 0;

                if (format == DYLD_CHAINED_PTR_64 || format == DYLD_CHAINED_PTR_64_OFFSET) {
                    bool is_bind = ((raw_val >> 63) & 1) != 0;
                    next_stride = (raw_val >> 51) & 0xFFFULL;

                    if (is_bind) {
                        std::uint32_t ordinal = static_cast<std::uint32_t>(raw_val & 0xFFFFFFULL);
                        std::int64_t addend = static_cast<std::int8_t>((raw_val >> 24) & 0xFFULL);

                        if (ordinal >= hdr->imports_count) {
                            out_error = "Chained fixup import ordinal out of range: " + std::to_string(ordinal);
                            return DyldLinkStatus::InvalidChainedFixups;
                        }

                        const auto* imp = reinterpret_cast<const DyldChainedImportRaw*>(
                            bytes + hdr->imports_offset + (static_cast<std::size_t>(ordinal) * sizeof(DyldChainedImportRaw)));

                        if (static_cast<std::size_t>(hdr->symbols_offset) + imp->name_offset >= size) {
                            out_error = "Chained fixup import symbol name out of bounds";
                            return DyldLinkStatus::InvalidChainedFixups;
                        }

                        const char* sym_name = reinterpret_cast<const char*>(bytes + hdr->symbols_offset + imp->name_offset);
                        auto resolved = resolve_symbol(sym_name, imp->lib_ordinal);

                        if (!resolved.has_value()) {
                            if (imp->weak_import) {
                                memory.write_u64(chain_loc, 0);
                            } else {
                                out_error = "Undefined dynamic symbol: " + std::string(sym_name);
                                return DyldLinkStatus::UndefinedSymbol;
                            }
                        } else {
                            std::uint64_t final_target = *resolved + static_cast<std::uint64_t>(addend);
                            memory.write_u64(chain_loc, final_target);
                        }
                    } else {
                        std::uint64_t target = raw_val & 0xFFFFFFFFFULL;
                        std::uint64_t high8 = (raw_val >> 36) & 0xFFULL;
                        std::uint64_t target_vaddr = 0;

                        if (format == DYLD_CHAINED_PTR_64_OFFSET) {
                            target_vaddr = base_vaddr + target + slide;
                        } else {
                            target_vaddr = ((high8 << 56) | target) + slide;
                        }
                        memory.write_u64(chain_loc, target_vaddr);
                    }

                    if (next_stride == 0) {
                        break;
                    }
                    chain_loc += next_stride * 4;
                } else {
                    // DYLD_CHAINED_PTR_ARM64E family
                    bool auth = ((raw_val >> 63) & 1) != 0;
                    bool is_bind = ((raw_val >> 62) & 1) != 0;
                    next_stride = (raw_val >> 51) & 0x7FFULL;

                    if (auth) {
                        if (is_bind) {
                            std::uint32_t ordinal = static_cast<std::uint32_t>(raw_val & 0xFFFFULL);
                            if (ordinal >= hdr->imports_count) {
                                out_error = "Chained fixup import ordinal out of range: " + std::to_string(ordinal);
                                return DyldLinkStatus::InvalidChainedFixups;
                            }

                            const auto* imp = reinterpret_cast<const DyldChainedImportRaw*>(
                                bytes + hdr->imports_offset + (static_cast<std::size_t>(ordinal) * sizeof(DyldChainedImportRaw)));

                            if (static_cast<std::size_t>(hdr->symbols_offset) + imp->name_offset >= size) {
                                out_error = "Chained fixup import symbol name out of bounds";
                                return DyldLinkStatus::InvalidChainedFixups;
                            }

                            const char* sym_name = reinterpret_cast<const char*>(bytes + hdr->symbols_offset + imp->name_offset);
                            auto resolved = resolve_symbol(sym_name, imp->lib_ordinal);

                            if (!resolved.has_value()) {
                                if (imp->weak_import) {
                                    memory.write_u64(chain_loc, 0);
                                } else {
                                    out_error = "Undefined dynamic symbol: " + std::string(sym_name);
                                    return DyldLinkStatus::UndefinedSymbol;
                                }
                            } else {
                                memory.write_u64(chain_loc, *resolved);
                            }
                        } else {
                            std::uint64_t target = raw_val & 0xFFFFFFFFULL;
                            std::uint64_t target_vaddr = base_vaddr + target + slide;
                            memory.write_u64(chain_loc, target_vaddr);
                        }
                    } else {
                        if (is_bind) {
                            std::uint32_t ordinal = static_cast<std::uint32_t>(raw_val & 0xFFFFULL);
                            std::int64_t addend = static_cast<std::int64_t>((raw_val >> 32) & 0x7FFFFULL);
                            if ((addend & 0x40000LL) != 0) {
                                addend |= ~0x7FFFFLL;
                            }

                            if (ordinal >= hdr->imports_count) {
                                out_error = "Chained fixup import ordinal out of range: " + std::to_string(ordinal);
                                return DyldLinkStatus::InvalidChainedFixups;
                            }

                            const auto* imp = reinterpret_cast<const DyldChainedImportRaw*>(
                                bytes + hdr->imports_offset + (static_cast<std::size_t>(ordinal) * sizeof(DyldChainedImportRaw)));

                            if (static_cast<std::size_t>(hdr->symbols_offset) + imp->name_offset >= size) {
                                out_error = "Chained fixup import symbol name out of bounds";
                                return DyldLinkStatus::InvalidChainedFixups;
                            }

                            const char* sym_name = reinterpret_cast<const char*>(bytes + hdr->symbols_offset + imp->name_offset);
                            auto resolved = resolve_symbol(sym_name, imp->lib_ordinal);

                            if (!resolved.has_value()) {
                                if (imp->weak_import) {
                                    memory.write_u64(chain_loc, 0);
                                } else {
                                    out_error = "Undefined dynamic symbol: " + std::string(sym_name);
                                    return DyldLinkStatus::UndefinedSymbol;
                                }
                            } else {
                                std::uint64_t final_target = *resolved + static_cast<std::uint64_t>(addend);
                                memory.write_u64(chain_loc, final_target);
                            }
                        } else {
                            std::uint64_t target = raw_val & 0x7FFFFFFFFFFULL;
                            std::uint64_t high8 = (raw_val >> 43) & 0xFFULL;
                            std::uint64_t target_vaddr = ((high8 << 56) | target) + slide;
                            memory.write_u64(chain_loc, target_vaddr);
                        }
                    }

                    if (next_stride == 0) {
                        break;
                    }
                    chain_loc += next_stride * 8;
                }
            }
        }
    }

    return DyldLinkStatus::Success;
}

DyldLinkStatus DyldEngine::validate_shared_cache_header(
    const void* data,
    std::size_t size,
    std::vector<DyldCacheMappingInfoRaw>& out_mappings,
    std::vector<std::string>& out_image_paths,
    std::string& out_error) {

    out_mappings.clear();
    out_image_paths.clear();

    if (!data || size < sizeof(DyldSharedCacheHeaderRaw)) {
        out_error = "Shared cache file smaller than header size";
        return DyldLinkStatus::SharedCacheCorrupt;
    }

    const auto* hdr = static_cast<const DyldSharedCacheHeaderRaw*>(data);

    // Verify Magic
    if (std::memcmp(hdr->magic, kDyldSharedCacheArm64eMagic, 16) != 0 &&
        std::memcmp(hdr->magic, kDyldSharedCacheArm64Magic, 16) != 0) {
        out_error = "Invalid shared cache magic";
        return DyldLinkStatus::SharedCacheCorrupt;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);

    // Validate and parse mappings
    if (hdr->mapping_offset + (hdr->mapping_count * sizeof(DyldCacheMappingInfoRaw)) > size) {
        out_error = "Shared cache mappings extend beyond file size";
        return DyldLinkStatus::SharedCacheCorrupt;
    }

    const auto* mappings = reinterpret_cast<const DyldCacheMappingInfoRaw*>(bytes + hdr->mapping_offset);
    for (std::uint32_t i = 0; i < hdr->mapping_count; ++i) {
        out_mappings.push_back(mappings[i]);
    }

    // Validate and parse images
    if (hdr->images_offset + (hdr->images_count * sizeof(DyldCacheImageInfoRaw)) > size) {
        out_error = "Shared cache image info table extends beyond file size";
        return DyldLinkStatus::SharedCacheCorrupt;
    }

    const auto* images = reinterpret_cast<const DyldCacheImageInfoRaw*>(bytes + hdr->images_offset);
    for (std::uint32_t i = 0; i < hdr->images_count; ++i) {
        std::uint32_t path_off = images[i].path_file_offset;
        if (path_off < size) {
            const char* s = reinterpret_cast<const char*>(bytes + path_off);
            std::size_t maxlen = size - path_off;
            std::size_t actual_len = 0;
            while (actual_len < maxlen && s[actual_len] != '\0') actual_len++;
            out_image_paths.push_back(std::string(s, actual_len));
        }
    }

    return DyldLinkStatus::Success;
}

std::vector<std::uint8_t> DyldFixtureBuilder::create_synthetic_shared_cache(
    const std::vector<std::string>& image_paths) {

    std::vector<std::uint8_t> buf;
    constexpr std::size_t kInitialSize = 0x4000;
    buf.assign(kInitialSize, 0);

    auto* hdr = reinterpret_cast<DyldSharedCacheHeaderRaw*>(buf.data());
    std::memcpy(hdr->magic, kDyldSharedCacheArm64eMagic, 16);
    hdr->dyld_base_address = 0x0000000180000000ULL;

    // Mappings
    hdr->mapping_offset = sizeof(DyldSharedCacheHeaderRaw);
    hdr->mapping_count = 3; // __TEXT, __DATA, __LINKEDIT
    auto* mappings = reinterpret_cast<DyldCacheMappingInfoRaw*>(buf.data() + hdr->mapping_offset);

    mappings[0].address = 0x180000000ULL;
    mappings[0].size = 0x10000000ULL;
    mappings[0].file_offset = 0;
    mappings[0].init_prot = 5; // R-X
    mappings[0].max_prot = 5;

    mappings[1].address = 0x190000000ULL;
    mappings[1].size = 0x08000000ULL;
    mappings[1].file_offset = 0x10000000ULL;
    mappings[1].init_prot = 3; // RW-
    mappings[1].max_prot = 3;

    mappings[2].address = 0x198000000ULL;
    mappings[2].size = 0x04000000ULL;
    mappings[2].file_offset = 0x18000000ULL;
    mappings[2].init_prot = 1; // R--
    mappings[2].max_prot = 1;

    // Image Info Table
    std::size_t images_offset = hdr->mapping_offset + (hdr->mapping_count * sizeof(DyldCacheMappingInfoRaw));
    hdr->images_offset = static_cast<std::uint32_t>(images_offset);
    hdr->images_count = static_cast<std::uint32_t>(image_paths.size());

    std::size_t path_pool_offset = images_offset + (hdr->images_count * sizeof(DyldCacheImageInfoRaw));
    auto* images = reinterpret_cast<DyldCacheImageInfoRaw*>(buf.data() + images_offset);

    for (std::size_t i = 0; i < image_paths.size(); ++i) {
        images[i].address = 0x180000000ULL + (i * 0x100000ULL);
        images[i].mod_time = 1700000000ULL;
        images[i].inode = 1000 + i;
        images[i].path_file_offset = static_cast<std::uint32_t>(path_pool_offset);

        if (path_pool_offset + image_paths[i].size() + 1 <= buf.size()) {
            std::memcpy(buf.data() + path_pool_offset, image_paths[i].c_str(), image_paths[i].size() + 1);
            path_pool_offset += (image_paths[i].size() + 1);
        }
    }

    return buf;
}

std::vector<std::uint8_t> DyldFixtureBuilder::create_synthetic_chained_fixups(
    const std::vector<std::string>& import_symbols) {

    std::vector<std::uint8_t> buf;
    constexpr std::size_t kSize = 0x1000;
    buf.assign(kSize, 0);

    auto* hdr = reinterpret_cast<DyldChainedFixupsHeaderRaw*>(buf.data());
    hdr->fixups_version = 0;
    hdr->starts_offset = 0x40;
    hdr->imports_offset = 0x80;
    hdr->imports_count = static_cast<std::uint32_t>(import_symbols.size());
    hdr->symbols_offset = 0x200;

    auto* imports = reinterpret_cast<DyldChainedImportRaw*>(buf.data() + hdr->imports_offset);
    std::size_t str_off = hdr->symbols_offset;

    for (std::size_t i = 0; i < import_symbols.size(); ++i) {
        imports[i].lib_ordinal = 1; // 1st dylib
        imports[i].weak_import = 0;
        imports[i].name_offset = static_cast<std::uint32_t>(str_off - hdr->symbols_offset);

        if (str_off + import_symbols[i].size() + 1 <= buf.size()) {
            std::memcpy(buf.data() + str_off, import_symbols[i].c_str(), import_symbols[i].size() + 1);
            str_off += (import_symbols[i].size() + 1);
        }
    }

    return buf;
}

std::vector<std::uint8_t> DyldFixtureBuilder::create_synthetic_chained_fixups_with_segments(
    const std::vector<std::string>& import_symbols,
    const std::vector<SyntheticSegmentFixups>& segments) {

    std::vector<std::uint8_t> buf(0x4000, 0);

    auto* hdr = reinterpret_cast<DyldChainedFixupsHeaderRaw*>(buf.data());
    hdr->fixups_version = 0;
    hdr->starts_offset = 0x40;

    auto* starts_img = reinterpret_cast<DyldChainedStartsInImageRaw*>(buf.data() + hdr->starts_offset);
    starts_img->seg_count = static_cast<std::uint32_t>(segments.size());

    std::size_t seg_info_offsets_loc = hdr->starts_offset + sizeof(std::uint32_t);
    std::size_t current_seg_write_pos = seg_info_offsets_loc + (segments.size() * sizeof(std::uint32_t));

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        std::uint32_t seg_offset_from_starts = static_cast<std::uint32_t>(current_seg_write_pos - hdr->starts_offset);
        std::memcpy(buf.data() + seg_info_offsets_loc + (i * sizeof(std::uint32_t)), &seg_offset_from_starts, sizeof(std::uint32_t));

        auto* seg_hdr = reinterpret_cast<DyldChainedStartsInSegmentRaw*>(buf.data() + current_seg_write_pos);
        seg_hdr->page_size = seg.page_size;
        seg_hdr->pointer_format = seg.pointer_format;
        seg_hdr->segment_offset = seg.segment_offset;
        seg_hdr->max_valid_pointer = 0;
        seg_hdr->page_count = static_cast<std::uint16_t>(seg.page_starts.size());
        seg_hdr->size = static_cast<std::uint32_t>(sizeof(DyldChainedStartsInSegmentRaw) + (seg.page_starts.size() * sizeof(std::uint16_t)));

        std::size_t page_starts_loc = current_seg_write_pos + sizeof(DyldChainedStartsInSegmentRaw);
        for (std::size_t p = 0; p < seg.page_starts.size(); ++p) {
            std::uint16_t pstart = seg.page_starts[p];
            std::memcpy(buf.data() + page_starts_loc + (p * sizeof(std::uint16_t)), &pstart, sizeof(std::uint16_t));
        }

        current_seg_write_pos = (page_starts_loc + (seg.page_starts.size() * sizeof(std::uint16_t)) + 7) & ~static_cast<std::size_t>(7ULL);
    }

    std::size_t imports_loc = (current_seg_write_pos < 0x800) ? 0x800 : ((current_seg_write_pos + 15) & ~static_cast<std::size_t>(15ULL));
    hdr->imports_offset = static_cast<std::uint32_t>(imports_loc);
    hdr->imports_count = static_cast<std::uint32_t>(import_symbols.size());
    hdr->imports_format = 1;

    std::size_t symbols_loc = imports_loc + (import_symbols.size() * sizeof(DyldChainedImportRaw));
    symbols_loc = (symbols_loc + 15) & ~static_cast<std::size_t>(15ULL);
    hdr->symbols_offset = static_cast<std::uint32_t>(symbols_loc);

    auto* imports = reinterpret_cast<DyldChainedImportRaw*>(buf.data() + imports_loc);
    std::size_t str_off = symbols_loc;

    for (std::size_t i = 0; i < import_symbols.size(); ++i) {
        imports[i].lib_ordinal = 1; // 1st dylib
        imports[i].weak_import = 0;
        imports[i].name_offset = static_cast<std::uint32_t>(str_off - symbols_loc);

        if (str_off + import_symbols[i].size() + 1 <= buf.size()) {
            std::memcpy(buf.data() + str_off, import_symbols[i].c_str(), import_symbols[i].size() + 1);
            str_off += (import_symbols[i].size() + 1);
        }
    }

    return buf;
}

std::vector<std::uint8_t> DyldFixtureBuilder::create_synthetic_chained_fixups_multi_lib(
    const std::vector<SyntheticImportDescriptor>& imports,
    const std::vector<SyntheticSegmentFixups>& segments) {

    std::vector<std::uint8_t> buf(0x4000, 0);

    auto* hdr = reinterpret_cast<DyldChainedFixupsHeaderRaw*>(buf.data());
    hdr->fixups_version = 0;
    hdr->starts_offset = 0x40;

    auto* starts_img = reinterpret_cast<DyldChainedStartsInImageRaw*>(buf.data() + hdr->starts_offset);
    starts_img->seg_count = static_cast<std::uint32_t>(segments.size());

    std::size_t seg_info_offsets_loc = hdr->starts_offset + sizeof(std::uint32_t);
    std::size_t current_seg_write_pos = seg_info_offsets_loc + (segments.size() * sizeof(std::uint32_t));

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        std::uint32_t seg_offset_from_starts = static_cast<std::uint32_t>(current_seg_write_pos - hdr->starts_offset);
        std::memcpy(buf.data() + seg_info_offsets_loc + (i * sizeof(std::uint32_t)), &seg_offset_from_starts, sizeof(std::uint32_t));

        auto* seg_hdr = reinterpret_cast<DyldChainedStartsInSegmentRaw*>(buf.data() + current_seg_write_pos);
        seg_hdr->page_size = seg.page_size;
        seg_hdr->pointer_format = seg.pointer_format;
        seg_hdr->segment_offset = seg.segment_offset;
        seg_hdr->max_valid_pointer = 0;
        seg_hdr->page_count = static_cast<std::uint16_t>(seg.page_starts.size());
        seg_hdr->size = static_cast<std::uint32_t>(sizeof(DyldChainedStartsInSegmentRaw) + (seg.page_starts.size() * sizeof(std::uint16_t)));

        std::size_t page_starts_loc = current_seg_write_pos + sizeof(DyldChainedStartsInSegmentRaw);
        for (std::size_t p = 0; p < seg.page_starts.size(); ++p) {
            std::uint16_t pstart = seg.page_starts[p];
            std::memcpy(buf.data() + page_starts_loc + (p * sizeof(std::uint16_t)), &pstart, sizeof(std::uint16_t));
        }

        current_seg_write_pos = (page_starts_loc + (seg.page_starts.size() * sizeof(std::uint16_t)) + 7) & ~static_cast<std::size_t>(7ULL);
    }

    std::size_t imports_loc = (current_seg_write_pos < 0x800) ? 0x800 : ((current_seg_write_pos + 15) & ~static_cast<std::size_t>(15ULL));
    hdr->imports_offset = static_cast<std::uint32_t>(imports_loc);
    hdr->imports_count = static_cast<std::uint32_t>(imports.size());
    hdr->imports_format = 1;

    std::size_t symbols_loc = imports_loc + (imports.size() * sizeof(DyldChainedImportRaw));
    symbols_loc = (symbols_loc + 15) & ~static_cast<std::size_t>(15ULL);
    hdr->symbols_offset = static_cast<std::uint32_t>(symbols_loc);

    auto* raw_imports = reinterpret_cast<DyldChainedImportRaw*>(buf.data() + imports_loc);
    std::size_t str_off = symbols_loc;

    for (std::size_t i = 0; i < imports.size(); ++i) {
        raw_imports[i].lib_ordinal = imports[i].lib_ordinal;
        raw_imports[i].weak_import = imports[i].weak_import ? 1 : 0;
        raw_imports[i].name_offset = static_cast<std::uint32_t>(str_off - symbols_loc);

        if (str_off + imports[i].name.size() + 1 <= buf.size()) {
            std::memcpy(buf.data() + str_off, imports[i].name.c_str(), imports[i].name.size() + 1);
            str_off += (imports[i].name.size() + 1);
        }
    }

    return buf;
}

std::vector<std::uint8_t> DyldFixtureBuilder::create_synthetic_export_trie(
    const std::vector<std::pair<std::string, std::uint64_t>>& exports) {

    std::vector<std::uint8_t> buf;

    struct ChildEntry {
        std::string   name;
        std::uint64_t address;
        std::size_t   offset_in_buf{0};
    };

    std::vector<ChildEntry> children;
    for (const auto& e : exports) {
        children.push_back({e.first, e.second, 0});
    }

    buf.push_back(0); // terminal_size = 0 for root
    buf.push_back(static_cast<std::uint8_t>(children.size()));

    struct PatchLoc {
        std::size_t patch_pos;
        std::size_t child_idx;
    };
    std::vector<PatchLoc> patch_locs;

    for (std::size_t i = 0; i < children.size(); ++i) {
        for (char c : children[i].name) {
            buf.push_back(static_cast<std::uint8_t>(c));
        }
        buf.push_back(0); // null terminator

        patch_locs.push_back({buf.size(), i});
        buf.push_back(0); // placeholder for child offset
    }

    for (std::size_t i = 0; i < children.size(); ++i) {
        children[i].offset_in_buf = buf.size();

        std::vector<std::uint8_t> term_payload;
        write_uleb128(term_payload, 0); // regular flag
        write_uleb128(term_payload, children[i].address);

        write_uleb128(buf, term_payload.size());
        buf.insert(buf.end(), term_payload.begin(), term_payload.end());
        buf.push_back(0); // child_count = 0
    }

    for (const auto& pl : patch_locs) {
        std::uint64_t child_off = children[pl.child_idx].offset_in_buf;
        buf[pl.patch_pos] = static_cast<std::uint8_t>(child_off & 0x7F);
    }

    return buf;
}

} // namespace winios::loader
