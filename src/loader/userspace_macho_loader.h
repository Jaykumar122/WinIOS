#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Mach-O Public Constants for Userspace
// -----------------------------------------------------------------------------
inline constexpr std::uint32_t kMachO64Magic             = 0xFEEDFACFu; // MH_MAGIC_64
inline constexpr std::uint32_t kMachO64Cigam             = 0xCFFAEDFEu; // MH_CIGAM_64

inline constexpr std::uint32_t kMachOCpuTypeARM64        = 0x0100000Cu; // CPU_TYPE_ARM64
inline constexpr std::uint32_t kMachOCpuSubtypeMask      = 0x00FFFFFFu;
inline constexpr std::uint32_t kMachOCpuSubtypeArm64All  = 0x00000000u;
inline constexpr std::uint32_t kMachOCpuSubtypeArm64V8   = 0x00000001u;
inline constexpr std::uint32_t kMachOCpuSubtypeArm64E    = 0x00000002u;
inline constexpr std::uint32_t kMachOCpuSubtypePtrAuth   = 0x80000000u; // CPU_SUBTYPE_PTRAUTH_ABI

// File Types
#ifndef WINIOS_MACHO_FILE_TYPES_DEFINED
#define WINIOS_MACHO_FILE_TYPES_DEFINED
inline constexpr std::uint32_t kMachOFileTypeExecute     = 0x00000002u; // MH_EXECUTE
inline constexpr std::uint32_t kMachOFileTypeDylib       = 0x00000006u; // MH_DYLIB
inline constexpr std::uint32_t kMachOFileTypeDylinker    = 0x00000007u; // MH_DYLINKER
inline constexpr std::uint32_t kMachOFileTypeBundle      = 0x00000008u; // MH_BUNDLE
#endif

// Header Flags
inline constexpr std::uint32_t kMachOFlagPie             = 0x00200000u; // MH_PIE
inline constexpr std::uint32_t kMachOFlagTwoLevel        = 0x00000080u; // MH_TWOLEVEL
inline constexpr std::uint32_t kMachOFlagDyldLink        = 0x00000004u; // MH_DYLDLINK

// Load Commands
inline constexpr std::uint32_t kCmdSegment64             = 0x00000019u; // LC_SEGMENT_64
inline constexpr std::uint32_t kCmdUnixThread            = 0x00000005u; // LC_UNIXTHREAD
inline constexpr std::uint32_t kCmdLoadDylinker          = 0x0000000Eu; // LC_LOAD_DYLINKER
inline constexpr std::uint32_t kCmdLoadDylib             = 0x0000000Cu; // LC_LOAD_DYLIB
inline constexpr std::uint32_t kCmdLoadWeakDylib         = 0x80000018u; // LC_LOAD_WEAK_DYLIB
inline constexpr std::uint32_t kCmdCodeSignature         = 0x0000001Du; // LC_CODE_SIGNATURE
inline constexpr std::uint32_t kCmdMain                  = 0x80000028u; // LC_MAIN
inline constexpr std::uint32_t kCmdBuildVersion          = 0x00000032u; // LC_BUILD_VERSION
inline constexpr std::uint32_t kCmdSourceVersion         = 0x0000002Au; // LC_SOURCE_VERSION
inline constexpr std::uint32_t kCmdRpath                 = 0x8000001Cu; // LC_RPATH
inline constexpr std::uint32_t kCmdDyldChainedFixups     = 0x80000034u; // LC_DYLD_CHAINED_FIXUPS
inline constexpr std::uint32_t kCmdDyldExportsTrie       = 0x80000033u; // LC_DYLD_EXPORTS_TRIE

// VM Protection
inline constexpr std::uint32_t kProtNone                 = 0x00000000u;
inline constexpr std::uint32_t kProtRead                 = 0x00000001u;
inline constexpr std::uint32_t kProtWrite                = 0x00000002u;
inline constexpr std::uint32_t kProtExecute              = 0x00000004u;

// Standard Apple Silicon Userspace Granule
inline constexpr std::uint64_t kApplePageSize16K         = 0x4000ULL; // 16 KB

// -----------------------------------------------------------------------------
// Mach-O On-Disk Structures
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct MachOHeader64Raw {
    std::uint32_t magic{0};
    std::uint32_t cputype{0};
    std::uint32_t cpusubtype{0};
    std::uint32_t filetype{0};
    std::uint32_t ncmds{0};
    std::uint32_t sizeofcmds{0};
    std::uint32_t flags{0};
    std::uint32_t reserved{0};
};

struct MachOLoadCommandRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
};

struct MachOSegment64Raw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    char          segname[16]{0};
    std::uint64_t vmaddr{0};
    std::uint64_t vmsize{0};
    std::uint64_t fileoff{0};
    std::uint64_t filesize{0};
    std::uint32_t maxprot{0};
    std::uint32_t initprot{0};
    std::uint32_t nsects{0};
    std::uint32_t flags{0};
};

struct MachOMainRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint64_t entryoff{0};
    std::uint64_t stacksize{0};
};

struct MachOBuildVersionRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t platform{0};
    std::uint32_t minos{0};
    std::uint32_t sdk{0};
    std::uint32_t ntools{0};
};

struct MachOLinkeditDataRaw {
    std::uint32_t cmd{0};
    std::uint32_t cmdsize{0};
    std::uint32_t dataoff{0};
    std::uint32_t datasize{0};
};

struct MachOSection64Raw {
    char          sectname[16]{0};
    char          segname[16]{0};
    std::uint64_t addr{0};
    std::uint64_t size{0};
    std::uint32_t offset{0};
    std::uint32_t align{0};
    std::uint32_t reloff{0};
    std::uint32_t nreloc{0};
    std::uint32_t flags{0};
    std::uint32_t reserved1{0};
    std::uint32_t reserved2{0};
    std::uint32_t reserved3{0};
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Parsed High-Level Information
// -----------------------------------------------------------------------------
struct UserspaceSegment {
    std::string   name;
    std::uint64_t vmaddr{0};
    std::uint64_t vmsize{0};
    std::uint64_t fileoff{0};
    std::uint64_t filesize{0};
    std::uint32_t initprot{0};
    std::uint32_t maxprot{0};
    bool          is_pagezero{false};
    std::uint64_t mapped_vaddr{0};

    [[nodiscard]] bool is_readable() const noexcept { return (initprot & kProtRead) != 0; }
    [[nodiscard]] bool is_writable() const noexcept { return (initprot & kProtWrite) != 0; }
    [[nodiscard]] bool is_executable() const noexcept { return (initprot & kProtExecute) != 0; }
};

struct UserspaceDylibDep {
    std::string   path;
    bool          is_weak{false};
    std::uint32_t current_version{0};
    std::uint32_t compatibility_version{0};
};

enum class UserspaceMachOLoadStatus {
    Success,
    InvalidMagic,
    UnsupportedCpuType,
    UnsupportedCpuSubtype,
    UnsupportedFileType,
    TruncatedHeader,
    CorruptedLoadCommands,
    SegmentOutOfBounds,
    OverlappingSegments,
    MisalignedSegment,
    IntegerOverflow,
    NoExecutableSegment,
    MissingEntryPoint,
    MemoryWriteFailed
};

[[nodiscard]] const char* userspace_macho_status_string(UserspaceMachOLoadStatus s) noexcept;

struct UserspaceLoadConfig {
    bool                     aslr_enabled{false};
    std::uint64_t            aslr_slide{0};
    std::uint64_t            stack_top{0x00007FFFFFE00000ULL}; // Standard 64-bit stack top
    std::uint64_t            stack_size{8ULL * 1024ULL * 1024ULL}; // 8 MB default
    std::uint64_t            page_size{kApplePageSize16K};
    std::vector<std::string> argv;
    std::vector<std::string> envp;
    std::string              executable_path;
};

struct UserspaceLoadResult {
    UserspaceMachOLoadStatus status{UserspaceMachOLoadStatus::TruncatedHeader};
    std::string              error_message;
    MachOHeader64Raw         header{};
    std::uint64_t            entry_point{0};
    std::uint64_t            initial_sp{0};
    std::uint64_t            aslr_slide{0};
    bool                     is_arm64e{false};
    bool                     is_pie{false};
    std::string              dylinker_path;
    std::vector<UserspaceDylibDep> dylibs;
    std::vector<std::string> rpaths;
    std::vector<UserspaceSegment>  segments;
    std::vector<std::uint64_t> mod_init_funcs;
    std::string              min_os_version;
    std::string              sdk_version;
    bool                     has_code_signature{false};
    std::uint32_t            code_signature_offset{0};
    std::uint32_t            code_signature_size{0};
    std::string              sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == UserspaceMachOLoadStatus::Success;
    }

    [[nodiscard]] const UserspaceSegment* find_segment(std::string_view name) const noexcept {
        for (const auto& s : segments) {
            if (s.name == name) return &s;
        }
        return nullptr;
    }
};

// -----------------------------------------------------------------------------
// UserspaceMachOLoader: Production 64-bit Userspace Mach-O Binary Loader
// -----------------------------------------------------------------------------
class UserspaceMachOLoader {
public:
    UserspaceMachOLoader() = default;
    ~UserspaceMachOLoader() = default;

    // Fast metadata parser without memory allocation or mapping
    [[nodiscard]] static bool parse_metadata(
        const void* data,
        std::size_t size,
        UserspaceLoadResult& out_result);

    // Segment validation (alignment, boundary, overlap, overflow)
    [[nodiscard]] static UserspaceMachOLoadStatus validate_segments(
        const std::vector<UserspaceSegment>& segments,
        std::size_t file_size,
        std::uint64_t page_size,
        std::string& out_error);

    // Load binary into guest memory and stage initial userspace stack
    [[nodiscard]] static UserspaceLoadResult load_into_memory(
        const void* data,
        std::size_t size,
        Memory& memory,
        const UserspaceLoadConfig& config);

    // Initial userspace stack builder conforming to ARM64 Darwin ABI
    [[nodiscard]] static bool construct_initial_stack(
        Memory& memory,
        std::uint64_t stack_top,
        const std::vector<std::string>& argv,
        const std::vector<std::string>& envp,
        const std::string& exec_path,
        std::uint64_t& out_sp,
        std::string& out_error);
};

// -----------------------------------------------------------------------------
// Synthetic Mach-O Test Fixture Generator
// -----------------------------------------------------------------------------
class UserspaceMachOFixtureBuilder {
public:
    static std::vector<std::uint8_t> create_valid_arm64e_executable(
        const std::string& dylinker = "/usr/lib/dyld",
        const std::vector<std::string>& dylibs = {"/usr/lib/libSystem.B.dylib"},
        const std::vector<std::uint8_t>& custom_code = {});

    static std::vector<std::uint8_t> create_valid_arm64_executable();
};

} // namespace winios::loader
