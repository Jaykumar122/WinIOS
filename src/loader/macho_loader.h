#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "loader/guest_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// Mach-O Magic Numbers and Constants
// -----------------------------------------------------------------------------
constexpr std::uint32_t kMachOMagic64    = 0xFEEDFACFu; // 64-bit Mach-O
constexpr std::uint32_t kMachOCigam64    = 0xCFFAEDFEu; // 64-bit Mach-O (byte-swapped)
constexpr std::uint32_t kMachOMagic32    = 0xFEEDFACEu; // 32-bit Mach-O
constexpr std::uint32_t kMachOFatMagic   = 0xCAFEBABEu; // Universal / Fat binary
constexpr std::uint32_t kMachOFatCigam   = 0xBEBAFECAu; // Fat binary (byte-swapped)

constexpr std::uint32_t kCpuTypeARM64    = 0x0100000Cu; // CPU_TYPE_ARM64 (CPU_ARCH_ABI64 | 12)
constexpr std::uint32_t kCpuSubtypeARM64All = 0x00000000u;
constexpr std::uint32_t kCpuSubtypeARM64V8  = 0x00000001u;

// Mach-O File Types
#ifndef WINIOS_MACHO_FILE_TYPES_DEFINED
#define WINIOS_MACHO_FILE_TYPES_DEFINED
inline constexpr std::uint32_t kMachOFileTypeExecute     = 0x00000002u;
inline constexpr std::uint32_t kMachOFileTypeDylib       = 0x00000006u;
inline constexpr std::uint32_t kMachOFileTypeDylinker    = 0x00000007u;
inline constexpr std::uint32_t kMachOFileTypeBundle      = 0x00000008u;
#endif
constexpr std::uint32_t kMachOFileTypeKextBundle  = 0x0000000Bu;
constexpr std::uint32_t kMachOFileTypeFileset     = 0x0000000Cu; // Modern iOS kernelcache

// Load Command Types
constexpr std::uint32_t kLoadCmdSegment64        = 0x00000019u; // LC_SEGMENT_64
constexpr std::uint32_t kLoadCmdUnixThread       = 0x00000005u; // LC_UNIXTHREAD
constexpr std::uint32_t kLoadCmdThread           = 0x00000004u; // LC_THREAD
constexpr std::uint32_t kLoadCmdMain             = 0x80000028u; // LC_MAIN
constexpr std::uint32_t kLoadCmdFilesetEntry     = 0x80000035u; // LC_FILESET_ENTRY

// ARM64 Thread State Flavor
constexpr std::uint32_t kArmThreadState64        = 0x00000006u;
constexpr std::uint32_t kArmThreadState64Count   = 68u; // Count of 32-bit words

// VM Protection Flags
constexpr std::uint32_t kVmProtNone              = 0x00000000u;
constexpr std::uint32_t kVmProtRead              = 0x00000001u;
constexpr std::uint32_t kVmProtWrite             = 0x00000002u;
constexpr std::uint32_t kVmProtExecute           = 0x00000004u;

// -----------------------------------------------------------------------------
// Mach-O On-Disk Structures
// -----------------------------------------------------------------------------
#pragma pack(push, 1)
struct MachOHeader64 {
    std::uint32_t magic;
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t filetype;
    std::uint32_t ncmds;
    std::uint32_t sizeofcmds;
    std::uint32_t flags;
    std::uint32_t reserved;
};

struct MachOFatHeader {
    std::uint32_t magic;
    std::uint32_t nfat_arch;
};

struct MachOFatArch {
    std::uint32_t cputype;
    std::uint32_t cpusubtype;
    std::uint32_t offset;
    std::uint32_t size;
    std::uint32_t align;
};

struct MachOLoadCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
};

struct MachOSegmentCommand64 {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    char segname[16];
    std::uint64_t vmaddr;
    std::uint64_t vmsize;
    std::uint64_t fileoff;
    std::uint64_t filesize;
    std::uint32_t maxprot;
    std::uint32_t initprot;
    std::uint32_t nsects;
    std::uint32_t flags;
};

struct MachOSection64 {
    char sectname[16];
    char segname[16];
    std::uint64_t addr;
    std::uint64_t size;
    std::uint32_t offset;
    std::uint32_t align;
    std::uint32_t reloff;
    std::uint32_t nreloc;
    std::uint32_t flags;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
    std::uint32_t reserved3;
};

struct MachOEntrypointCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint64_t entryoff;
    std::uint64_t stacksize;
};

struct MachOFilesetEntryCommand {
    std::uint32_t cmd;
    std::uint32_t cmdsize;
    std::uint64_t vmaddr;
    std::uint64_t fileoff;
    std::uint32_t entry_id_offset;
    std::uint32_t reserved;
};

struct MachOArmThreadState64 {
    std::uint64_t x[29];
    std::uint64_t fp;
    std::uint64_t lr;
    std::uint64_t sp;
    std::uint64_t pc;
    std::uint32_t cpsr;
    std::uint32_t pad;
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// Parsed Segment Information
// -----------------------------------------------------------------------------
struct MachOSegmentInfo {
    std::string name;
    std::uint64_t vmaddr{0};
    std::uint64_t vmsize{0};
    std::uint64_t fileoff{0};
    std::uint64_t filesize{0};
    std::uint32_t initprot{kVmProtRead};
    std::uint32_t maxprot{kVmProtRead | kVmProtWrite | kVmProtExecute};
    std::uint64_t mapped_address{0};
};

struct MachOFilesetEntryInfo {
    std::string entry_id;
    std::uint64_t vmaddr{0};
    std::uint64_t fileoff{0};
};

// -----------------------------------------------------------------------------
// Mach-O Load Status & Result
// -----------------------------------------------------------------------------
enum class MachOLoadStatus {
    Success,
    FileNotFound,
    FileReadError,
    InvalidMagic,
    UnsupportedCpuType,
    TruncatedHeader,
    CorruptedLoadCommands,
    NoSegmentsFound,
    MemoryBoundsExceeded,
    MemoryWriteFailed
};

struct MachOLoadResult {
    MachOLoadStatus status{MachOLoadStatus::FileNotFound};
    std::string error_message;
    MachOHeader64 header{};
    std::uint64_t entry_point{0};
    std::uint64_t initial_sp{0};
    std::uint64_t initial_lr{0};
    std::uint32_t initial_cpsr{0};
    std::uint64_t lowest_vmaddr{0};
    std::uint64_t highest_vmaddr{0};
    std::uint64_t total_vmsize{0};
    std::vector<MachOSegmentInfo> segments;
    std::vector<MachOFilesetEntryInfo> fileset_entries;
    std::string sha256_hex;

    [[nodiscard]] bool is_success() const noexcept {
        return status == MachOLoadStatus::Success;
    }

    [[nodiscard]] const MachOSegmentInfo* find_segment(std::string_view name) const noexcept {
        for (const auto& s : segments) {
            if (s.name == name) return &s;
        }
        return nullptr;
    }

    [[nodiscard]] const MachOFilesetEntryInfo* find_fileset_entry(std::string_view id) const noexcept {
        for (const auto& e : fileset_entries) {
            if (e.entry_id == id) return &e;
        }
        return nullptr;
    }
};

// -----------------------------------------------------------------------------
// MachOLoader: High-performance 64-bit Mach-O & Kernelcache Loader
// -----------------------------------------------------------------------------
class MachOLoader {
public:
    MachOLoader() = default;
    ~MachOLoader() = default;

    // Load from filesystem path into guest RAM
    [[nodiscard]] static MachOLoadResult load_from_file(
        const std::string& path,
        Memory& guest_mem,
        std::uint64_t physical_base,
        std::uint64_t ram_size,
        std::uint64_t kaslr_slide = 0,
        GuestMemoryMap* memory_map = nullptr);

    // Load from memory buffer into guest RAM
    [[nodiscard]] static MachOLoadResult load_from_buffer(
        const void* data,
        std::size_t size,
        Memory& guest_mem,
        std::uint64_t physical_base,
        std::uint64_t ram_size,
        std::uint64_t kaslr_slide = 0,
        GuestMemoryMap* memory_map = nullptr);

    // Header & metadata parsing without memory write
    [[nodiscard]] static bool parse_metadata(
        const void* data,
        std::size_t size,
        MachOLoadResult& out_result);

    // Universal fat binary unwrapping to locate ARM64 slice
    [[nodiscard]] static bool locate_arm64_slice(
        const void* data,
        std::size_t size,
        std::size_t& out_slice_offset,
        std::size_t& out_slice_size);
};

} // namespace winios::loader
