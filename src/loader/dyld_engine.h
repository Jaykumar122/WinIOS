#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// dyld Shared Cache & Chained Fixups Raw Constants & Headers
// -----------------------------------------------------------------------------
inline constexpr char kDyldSharedCacheArm64eMagic[16] = "dyld_v1  arm64e";
inline constexpr char kDyldSharedCacheArm64Magic[16]  = "dyld_v1   arm64";

#pragma pack(push, 1)
struct DyldSharedCacheHeaderRaw {
    char          magic[16]{0};
    std::uint32_t mapping_offset{0};
    std::uint32_t mapping_count{0};
    std::uint32_t images_offset{0};
    std::uint32_t images_count{0};
    std::uint64_t dyld_base_address{0};
    std::uint64_t code_signature_offset{0};
    std::uint64_t code_signature_size{0};
    std::uint64_t slide_info_offset{0};
    std::uint64_t slide_info_size{0};
    std::uint64_t local_symbols_offset{0};
    std::uint64_t local_symbols_size{0};
};

struct DyldCacheMappingInfoRaw {
    std::uint64_t address{0};
    std::uint64_t size{0};
    std::uint64_t file_offset{0};
    std::uint32_t max_prot{0};
    std::uint32_t init_prot{0};
};

struct DyldCacheImageInfoRaw {
    std::uint64_t address{0};
    std::uint64_t mod_time{0};
    std::uint64_t inode{0};
    std::uint32_t path_file_offset{0};
    std::uint32_t pad{0};
};

struct DyldChainedFixupsHeaderRaw {
    std::uint32_t fixups_version{0};
    std::uint32_t starts_offset{0};
    std::uint32_t imports_offset{0};
    std::uint32_t symbols_offset{0};
    std::uint32_t imports_count{0};
    std::uint32_t imports_format{1}; // 1 = DYLD_CHAINED_IMPORT
    std::uint32_t symbols_format{0}; // 0 = uncompressed strings
};

struct DyldChainedImportRaw {
    std::uint32_t lib_ordinal : 8;
    std::uint32_t weak_import : 1;
    std::uint32_t name_offset : 23;
};

inline constexpr std::uint16_t DYLD_CHAINED_PTR_START_NONE = 0xFFFF;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_START_MULTI = 0x8000;

inline constexpr std::uint16_t DYLD_CHAINED_PTR_ARM64E           = 1;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_64               = 2;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_32               = 3;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_32_CACHE         = 4;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_32_FIRMWARE      = 5;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_64_OFFSET        = 6;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_ARM64E_KERNEL    = 7;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_64_KERNEL_CACHE  = 8;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_ARM64E_USERLAND  = 9;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_ARM64E_FIRMWARE  = 10;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE = 11;
inline constexpr std::uint16_t DYLD_CHAINED_PTR_ARM64E_USERLAND24 = 12;

struct DyldChainedStartsInImageRaw {
    std::uint32_t seg_count{0};
};

struct DyldChainedStartsInSegmentRaw {
    std::uint32_t size{0};
    std::uint16_t page_size{0x4000};
    std::uint16_t pointer_format{DYLD_CHAINED_PTR_64};
    std::uint64_t segment_offset{0};
    std::uint32_t max_valid_pointer{0};
    std::uint16_t page_count{0};
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Higher-Level Dyld Structures
// -----------------------------------------------------------------------------
enum class DyldSymbolType {
    Undefined,
    Absolute,
    Section,
    Prebound
};

struct DyldSymbol {
    std::string    name;
    std::uint64_t  address{0};
    DyldSymbolType type{DyldSymbolType::Section};
    bool           is_external{true};
    bool           is_weak{false};
    std::uint32_t  dylib_ordinal{0};
};

struct DyldBindingRecord {
    std::string   symbol_name;
    std::uint64_t patch_vaddr{0};
    std::int64_t  addend{0};
    std::uint32_t dylib_ordinal{0};
    bool          is_weak{false};
};

struct DyldRebaseRecord {
    std::uint64_t patch_vaddr{0};
    std::uint8_t  type{1}; // 1 = POINTER
};

enum class DyldLinkStatus {
    Success,
    DylibNotFound,
    UndefinedSymbol,
    InvalidChainedFixups,
    SharedCacheCorrupt,
    MemoryPatchFailed,
    RelocationOverflow,
    MalformedExportTrie,
    MalformedSymtab
};

inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_KIND_MASK         = 0x03;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_KIND_REGULAR      = 0x00;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL = 0x01;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_KIND_ABSOLUTE     = 0x02;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION   = 0x04;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_REEXPORT          = 0x08;
inline constexpr std::uint32_t EXPORT_SYMBOL_FLAGS_STUB_AND_RESOLVER = 0x10;

inline constexpr std::int32_t BIND_SPECIAL_DYLIB_SELF            = 0;
inline constexpr std::int32_t BIND_SPECIAL_DYLIB_MAIN_EXECUTABLE = -1;
inline constexpr std::int32_t BIND_SPECIAL_DYLIB_FLAT_LOOKUP     = -2;
inline constexpr std::int32_t BIND_SPECIAL_DYLIB_WEAK_LOOKUP     = -3;

struct DyldExportedSymbolInfo {
    std::string   name;
    std::uint64_t address{0};
    std::uint32_t flags{0};
    std::uint32_t other_ordinal{0};
    std::string   reexport_name;
    bool          is_weak_definition{false};
    bool          is_reexport{false};
};

struct DyldSymtabSymbolInfo {
    std::string   name;
    std::uint64_t address{0};
    std::uint8_t  type{0};
    std::uint8_t  sect{0};
    std::uint16_t desc{0};
    bool          is_external{false};
    bool          is_weak_def{false};
    bool          is_weak_ref{false};
};

[[nodiscard]] const char* dyld_link_status_string(DyldLinkStatus s) noexcept;

struct DyldLoadedModule {
    std::string                    name;
    std::string                    install_name;
    std::uint64_t                  base_address{0};
    std::uint64_t                  slide{0};
    std::map<std::string, std::uint64_t> exports;
    std::vector<DyldBindingRecord> binding_records;
    std::vector<DyldRebaseRecord>  rebase_records;
};

// -----------------------------------------------------------------------------
// DyldEngine: Dynamic Linker, Symbol Resolver & Chained Fixups Processor
// -----------------------------------------------------------------------------
class DyldEngine {
public:
    DyldEngine();
    ~DyldEngine() = default;

    // Register a loaded dylib image into the dynamic linker scope
    void register_module(DyldLoadedModule module);

    // Register standard iOS system dylibs with mock runtime exports (libSystem.B, libc, libdispatch)
    void register_builtin_system_libraries();

    // Clear all registered modules
    void clear_modules() noexcept;

    // Get 1-based ordinal of a registered module by name or install name (returns 0 if not found)
    [[nodiscard]] std::uint32_t get_module_ordinal(std::string_view name) const noexcept;

    // Query whether a module is loaded
    [[nodiscard]] bool has_module(std::string_view name) const noexcept;
    [[nodiscard]] const DyldLoadedModule* find_module(std::string_view name) const noexcept;

    // Resolve an exported symbol across all registered modules
    [[nodiscard]] std::optional<std::uint64_t> resolve_symbol(
        std::string_view name,
        std::uint32_t ordinal = 0) const noexcept;

    // Apply rebases to guest memory (adds ASLR slide to internal pointers)
    [[nodiscard]] DyldLinkStatus apply_rebases(
        Memory& memory,
        const std::vector<DyldRebaseRecord>& rebases,
        std::uint64_t slide,
        std::uint64_t base_vaddr,
        std::string& out_error);

    // Apply bindings to guest memory (writes target virtual address to binding locations)
    [[nodiscard]] DyldLinkStatus apply_bindings(
        Memory& memory,
        const std::vector<DyldBindingRecord>& bindings,
        std::uint64_t base_vaddr,
        std::string& out_error);

    // Parse and apply modern LC_DYLD_CHAINED_FIXUPS blob
    [[nodiscard]] DyldLinkStatus process_chained_fixups(
        const void* data,
        std::size_t size,
        Memory& memory,
        std::uint64_t base_vaddr,
        std::uint64_t slide,
        std::string& out_error);

    // Parse and validate Apple dyld shared cache header and metadata
    [[nodiscard]] static DyldLinkStatus validate_shared_cache_header(
        const void* data,
        std::size_t size,
        std::vector<DyldCacheMappingInfoRaw>& out_mappings,
        std::vector<std::string>& out_image_paths,
        std::string& out_error);

    // Resolve symbol using two-level namespace semantics
    [[nodiscard]] DyldLinkStatus resolve_symbol_two_level(
        std::string_view name,
        std::int32_t library_ordinal,
        bool is_weak_import,
        std::optional<std::uint64_t>& out_address,
        std::string& out_error) const noexcept;

    // Parse modern Mach-O Export Trie (LC_DYLD_INFO_ONLY export_info or LC_DYLD_EXPORTS_TRIE)
    [[nodiscard]] static bool parse_export_trie(
        const void* data,
        std::size_t size,
        std::vector<DyldExportedSymbolInfo>& out_symbols,
        std::string& out_error);

    // Parse classic Mach-O symbol table (LC_SYMTAB)
    [[nodiscard]] static bool parse_symbol_table(
        const void* macho_data,
        std::size_t macho_size,
        std::uint32_t symoff,
        std::uint32_t nsyms,
        std::uint32_t stroff,
        std::uint32_t strsize,
        std::vector<DyldSymtabSymbolInfo>& out_symbols,
        std::string& out_error);

    // Parse modern or classic LC_DYLD_INFO / LC_DYLD_INFO_ONLY binding opcodes
    [[nodiscard]] static bool parse_binding_stream(
        const void* data,
        std::size_t size,
        const std::vector<std::uint64_t>& segment_vmaddrs,
        std::vector<DyldBindingRecord>& out_bindings,
        std::string& out_error);

private:
    std::vector<DyldLoadedModule> modules_;
};

// -----------------------------------------------------------------------------
// Dyld Test Fixture Builder
// -----------------------------------------------------------------------------
class DyldFixtureBuilder {
public:
    struct SyntheticSegmentFixups {
        std::uint64_t segment_offset{0};
        std::uint16_t page_size{0x1000};
        std::uint16_t pointer_format{DYLD_CHAINED_PTR_64};
        std::vector<std::uint16_t> page_starts;
    };

    struct SyntheticImportDescriptor {
        std::string   name;
        std::uint8_t  lib_ordinal{1};
        bool          weak_import{false};
    };

    static std::vector<std::uint8_t> create_synthetic_shared_cache(
        const std::vector<std::string>& image_paths);

    static std::vector<std::uint8_t> create_synthetic_chained_fixups(
        const std::vector<std::string>& import_symbols);

    static std::vector<std::uint8_t> create_synthetic_chained_fixups_with_segments(
        const std::vector<std::string>& import_symbols,
        const std::vector<SyntheticSegmentFixups>& segments);

    static std::vector<std::uint8_t> create_synthetic_chained_fixups_multi_lib(
        const std::vector<SyntheticImportDescriptor>& imports,
        const std::vector<SyntheticSegmentFixups>& segments);

    static std::vector<std::uint8_t> create_synthetic_export_trie(
        const std::vector<std::pair<std::string, std::uint64_t>>& exports);
};

} // namespace winios::loader
