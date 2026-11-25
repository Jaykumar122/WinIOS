#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cpu/pointer_auth.h"
#include "loader/guest_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

// Provenance classification for every symbol and instruction region in a
// bounded Mach-O MH_OBJECT integration session.
enum class SymbolProvenanceClassification {
    GenuineUpstream,
    WinIOSArchitectural,
    TestHarnessStub,
    UnresolvedExternal,
    UnsupportedSymbol
};

[[nodiscard]] const char* symbol_provenance_string(SymbolProvenanceClassification c) noexcept;

enum class ObjectIntegrationStatus {
    Success,
    InvalidMagic,
    UnsupportedCpuType,
    NotObjectFile,
    TruncatedHeader,
    CorruptedLoadCommands,
    InvalidSectionBounds,
    MalformedSymbolTable,
    UnresolvedExternal,
    UnsupportedRelocationType,
    RelocationOutOfBounds,
    RelocationRangeOverflow,
    RelocationOpcodeMismatch,
    WxPermissionViolation,
    MemoryBoundsExceeded,
    IntegerOverflow,
    FileReadError
};

[[nodiscard]] const char* object_integration_status_string(ObjectIntegrationStatus s) noexcept;

struct MachORelocEntry {
    std::int32_t  r_address{0};
    std::uint32_t symbol_index{0};
    bool          pcrel{false};
    std::uint8_t  length{0};     // log2 size: 2 -> 4 bytes
    bool          is_extern{false};
    std::uint8_t  type{0};       // e.g. 2 = ARM64_RELOC_BRANCH26
    std::string   target_symbol;
    SymbolProvenanceClassification target_classification{SymbolProvenanceClassification::UnresolvedExternal};
    std::uint64_t site_va{0};
    std::uint64_t target_va{0};
    std::uint32_t original_insn{0};
    std::uint32_t patched_insn{0};
    std::uint64_t original_qword{0};
    std::uint64_t patched_qword{0};
    bool          patched{false};
    std::int64_t  addend{0};
    std::string   owner_object;
};

struct MachOObjectSectionInfo {
    std::string   sectname;
    std::string   segname;
    std::uint64_t addr{0};
    std::uint64_t size{0};
    std::uint32_t offset{0};
    std::uint32_t align_log2{0};
    std::uint32_t reloff{0};
    std::uint32_t nreloc{0};
    std::uint32_t flags{0};
    bool          is_debug{false};
    bool          is_executable{false};
    bool          is_zerofill{false};
    std::vector<std::uint8_t> bytes;
    std::vector<MachORelocEntry> relocations;
};

struct MachOSymbolEntry {
    std::uint32_t index{0};
    std::string   name;
    std::uint8_t  n_type{0};
    std::uint8_t  n_sect{0};
    std::uint16_t n_desc{0};
    std::uint64_t n_value{0};
    bool          is_stab{false};
    bool          is_external{false};
    bool          is_defined{false};
    bool          is_undefined{false};
    bool          is_common{false};
    bool          is_resolved{false};
    SymbolProvenanceClassification classification{SymbolProvenanceClassification::UnresolvedExternal};
    std::uint64_t resolved_va{0};
};

struct MachOObjectParseResult {
    ObjectIntegrationStatus status{ObjectIntegrationStatus::TruncatedHeader};
    std::string error_message;
    std::uint32_t magic{0};
    std::uint32_t cputype{0};
    std::uint32_t cpusubtype{0};
    std::uint32_t filetype{0};
    std::uint32_t ncmds{0};
    std::uint32_t sizeofcmds{0};
    std::uint32_t flags{0};
    std::string object_sha256_hex;

    std::vector<MachOObjectSectionInfo> sections;
    std::vector<MachOSymbolEntry> symbols;

    [[nodiscard]] bool is_success() const noexcept {
        return status == ObjectIntegrationStatus::Success;
    }

    [[nodiscard]] const MachOObjectSectionInfo* find_section(
        const std::string& segname,
        const std::string& sectname) const noexcept;

    [[nodiscard]] const MachOSymbolEntry* find_symbol(const std::string& name) const noexcept;
};

struct BoundedExternalBinding {
    std::string symbol_name;
    SymbolProvenanceClassification classification{SymbolProvenanceClassification::TestHarnessStub};
    // Optional stub instructions emitted into the separate __STUB_TEXT RX region.
    // Default single RET if empty and prebound_va == 0.
    std::vector<std::uint32_t> stub_instructions;
    // Optional pre-existing VA instead of allocating a stub slot.
    std::uint64_t prebound_va{0};
    std::string description;
};

struct MachOIntegrationOptions {
    // Base VA for genuine upstream object __TEXT (must be 16 KiB aligned for RX staging).
    std::uint64_t text_base_va{0x10000000ULL};
    // Base VA for separate stub/trampoline page (16 KiB aligned, distinct from text_base_va).
    std::uint64_t stub_base_va{0x10008000ULL};
    // Physical guest RAM translation offset (va - physical_base_offset) or 1:1 if 0.
    // When staging into flat Memory, guest_ram_base maps virtual addresses into [0, mem.size()):
    // phys = va - virtual_to_phys_bias.
    std::uint64_t virtual_base_bias{0x10000000ULL};
    // Provenance label for defined object text sections (cannot spoof TestHarnessStub as GenuineUpstream
    // because external bindings always record their own classification separately).
    std::string upstream_source_label{"xnu-10002.1.13/osfmk/arm64/cswitch.o"};
    // If true, unresolved external symbols referenced by relocations fail integration strictly.
    bool require_all_relocations_resolved{true};
    // Explicit external bindings (must explicitly state classification; UnresolvedExternal rejected).
    std::vector<BoundedExternalBinding> external_bindings;
};

struct MachOProvenanceRegion {
    std::uint64_t va_start{0};
    std::uint64_t size{0};
    std::uint32_t vm_prot{0}; // bit0=R, bit1=W, bit2=X; must satisfy !(W && X)
    SymbolProvenanceClassification classification{SymbolProvenanceClassification::UnresolvedExternal};
    std::string label;
};

struct MachOIntegrationResult {
    ObjectIntegrationStatus status{ObjectIntegrationStatus::TruncatedHeader};
    std::string error_message;
    std::string object_sha256_hex;

    std::uint64_t text_va{0};
    std::uint64_t text_size{0};
    std::uint64_t stub_va{0};
    std::uint64_t stub_size{0};

    std::vector<MachOSymbolEntry> symbols;
    std::vector<MachORelocEntry> relocations;
    std::vector<MachOProvenanceRegion> provenance_regions;
    std::vector<std::uint8_t> relocated_text_bytes;
    std::vector<std::uint8_t> stub_bytes;

    // Explicit audit flags: honest distinction between pure upstream bytes and stubbed linkage.
    bool has_test_harness_stubs{false};
    bool has_winios_architectural_bindings{false};
    std::vector<std::string> unresolved_symbols;
    std::vector<std::string> stub_symbols;

    [[nodiscard]] bool is_success() const noexcept {
        return status == ObjectIntegrationStatus::Success;
    }

    [[nodiscard]] bool find_symbol_va(const std::string& name, std::uint64_t& out_va) const noexcept;
    [[nodiscard]] SymbolProvenanceClassification classify_va(std::uint64_t va) const noexcept;
};

struct MachOBoundedContainerResult {
    ObjectIntegrationStatus status{ObjectIntegrationStatus::TruncatedHeader};
    std::string error_message;
    std::vector<std::uint8_t> macho_execute_bytes;
    std::string container_sha256_hex;
    MachOIntegrationResult link_audit;

    [[nodiscard]] bool is_success() const noexcept {
        return status == ObjectIntegrationStatus::Success;
    }
};

struct MachOObjectInput {
    std::string source_label;
    MachOObjectParseResult parsed;
};

struct MachOStagedSection {
    std::string object_label;
    std::string segname;
    std::string sectname;
    std::uint64_t va{0};
    std::uint64_t size{0};
    std::uint32_t vm_prot{0}; // bit0=R, bit1=W, bit2=X; must satisfy !(W && X)
    bool is_zerofill{false};
    std::vector<std::uint8_t> bytes;
};

struct MachOGotSlot {
    std::string symbol_name;
    std::uint64_t slot_va{0};
    std::uint64_t target_va{0};
    SymbolProvenanceClassification target_classification{SymbolProvenanceClassification::UnresolvedExternal};
};

struct MachOMultiObjectOptions {
    // Base VA for genuine upstream RX __TEXT,__text sections (16 KiB aligned).
    std::uint64_t text_base_va{0x10000000ULL};
    // Base VA for RO sections (__TEXT,__cstring/__const, __DATA_CONST, synthetic __got) (16 KiB aligned).
    std::uint64_t rodata_base_va{0x10020000ULL};
    // Base VA for RW sections (__DATA,*, __BOOTDATA,*) (16 KiB aligned).
    std::uint64_t data_base_va{0x10040000ULL};
    // Base VA for explicit external stubs (16 KiB aligned RX).
    std::uint64_t stub_base_va{0x10080000ULL};
    // Guest physical translation bias: phys = va - virtual_base_bias.
    std::uint64_t virtual_base_bias{0x10000000ULL};
    bool require_all_relocations_resolved{false};
    bool reject_duplicate_strong_symbols{true};
    std::vector<BoundedExternalBinding> external_bindings;
    // If true, sign ARM64_RELOC_AUTHENTICATED_POINTER slots with authenticated_pointer_keys.
    // If false, authenticated-pointer relocations are validated/audited and left unsigned
    // (rejected when require_all_relocations_resolved is true).
    bool enable_authenticated_pointer_signing{false};
    winios::cpu::PacKeyRing authenticated_pointer_keys{};
    // If true, synthesize section$start$/section$end$/segment$start$/segment$end$ symbols
    // from placed staged sections when referenced as undefined externals.
    bool synthesize_section_boundary_symbols{true};
};

struct MachOMultiObjectLinkResult {
    ObjectIntegrationStatus status{ObjectIntegrationStatus::TruncatedHeader};
    std::string error_message;

    std::uint64_t text_va{0};
    std::uint64_t text_size{0};
    std::uint64_t rodata_va{0};
    std::uint64_t rodata_size{0};
    std::uint64_t data_va{0};
    std::uint64_t data_size{0};
    std::uint64_t stub_va{0};
    std::uint64_t stub_size{0};

    std::vector<MachOStagedSection> staged_sections;
    std::vector<MachOGotSlot> got_slots;
    std::vector<MachOSymbolEntry> resolved_symbols;
    std::vector<MachORelocEntry> relocations;
    std::vector<MachOProvenanceRegion> provenance_regions;

    std::vector<std::string> unresolved_symbols;
    std::vector<std::string> stub_symbols;
    bool has_test_harness_stubs{false};
    bool has_winios_architectural_bindings{false};
    std::size_t unpatched_authenticated_relocations{0};
    bool has_unpatched_relocations{false};

    [[nodiscard]] bool is_success() const noexcept {
        return status == ObjectIntegrationStatus::Success;
    }

    [[nodiscard]] bool find_symbol_va(const std::string& name, std::uint64_t& out_va) const noexcept;
    [[nodiscard]] bool find_symbol(const std::string& name, MachOSymbolEntry& out_sym) const noexcept;
    [[nodiscard]] SymbolProvenanceClassification classify_va(std::uint64_t va) const noexcept;
};

struct MachOMultiObjectContainerResult {
    ObjectIntegrationStatus status{ObjectIntegrationStatus::TruncatedHeader};
    std::string error_message;
    std::vector<std::uint8_t> macho_execute_bytes;
    std::string container_sha256_hex;
    MachOMultiObjectLinkResult link_audit;

    [[nodiscard]] bool is_success() const noexcept {
        return status == ObjectIntegrationStatus::Success;
    }
};

// Bounded ARM64 Mach-O MH_OBJECT parser, relocator, provenance auditor, and W^X stager.
// Deliberately separate from KernelImageParser and MachoUserLoader, which must continue
// rejecting MH_OBJECT at boot/userspace boundaries.
class MachOObjectIntegrator {
public:
    static constexpr std::uint32_t kPageGranuleBytes = 16384u; // 16 KiB Apple ARM64 page

    [[nodiscard]] static MachOObjectParseResult parse_object(
        const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] static MachOObjectParseResult parse_object_file(
        const std::string& path);

    [[nodiscard]] static MachOIntegrationResult stage_in_memory(
        const MachOObjectParseResult& parsed,
        const MachOIntegrationOptions& opts,
        Memory& guest_mem,
        GuestMemoryMap* memory_map = nullptr);

    // Relocate and resolve symbols without mutating guest Memory (for link inspection / container build).
    [[nodiscard]] static MachOIntegrationResult link_object(
        const MachOObjectParseResult& parsed,
        const MachOIntegrationOptions& opts);

    // Multi-object bounded linker supporting BRANCH26, PAGE21, PAGEOFF12, GOT_LOAD_PAGE21,
    // GOT_LOAD_PAGEOFF12, UNSIGNED, SUBTRACTOR, ADDEND, and AUTHENTICATED_POINTER relocations
    // (including r_extern == 0 section-relative targets) with strict W^X & provenance enforcement.
    [[nodiscard]] static MachOMultiObjectLinkResult link_objects(
        const std::vector<MachOObjectInput>& inputs,
        const MachOMultiObjectOptions& opts);

    [[nodiscard]] static MachOMultiObjectLinkResult stage_objects_in_memory(
        const std::vector<MachOObjectInput>& inputs,
        const MachOMultiObjectOptions& opts,
        Memory& guest_mem,
        GuestMemoryMap* memory_map = nullptr);

    // Build a bounded standalone MH_EXECUTE Mach-O binary from the linked object + optional stub page.
    [[nodiscard]] static MachOBoundedContainerResult build_bounded_macho_execute(
        const MachOObjectParseResult& parsed,
        const MachOIntegrationOptions& opts,
        const std::string& entry_symbol = "_machine_load_context");

    // Build a bounded standalone MH_EXECUTE Mach-O binary from multiple linked upstream objects.
    [[nodiscard]] static MachOMultiObjectContainerResult build_bounded_multi_object_execute(
        const std::vector<MachOObjectInput>& inputs,
        const MachOMultiObjectOptions& opts,
        const std::string& entry_symbol = "_machine_load_context");

    // Helpers: decode/encode ARM64 relocation instruction forms.
    [[nodiscard]] static bool encode_branch26(
        std::uint32_t original_insn,
        std::uint64_t pc_va,
        std::uint64_t target_va,
        std::uint32_t& out_patched_insn,
        std::string& out_error);

    [[nodiscard]] static bool encode_page21(
        std::uint32_t original_insn,
        std::uint64_t pc_va,
        std::uint64_t target_va,
        std::uint32_t& out_patched_insn,
        std::string& out_error);

    [[nodiscard]] static bool encode_pageoff12(
        std::uint32_t original_insn,
        std::uint64_t target_va,
        std::uint32_t& out_patched_insn,
        std::string& out_error);
};

} // namespace winios::loader
