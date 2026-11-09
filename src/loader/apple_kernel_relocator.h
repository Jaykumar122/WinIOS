#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "loader/apple_kernel_loader.h"
#include "loader/macho_loader.h"
#include "memory/apple_physical_memory_map.h"
#include "memory/memory.h"

namespace winios::loader {

// -----------------------------------------------------------------------------
// KernelRelocationType: Classification of Apple ARM64 / ARM64e relocations
// -----------------------------------------------------------------------------
enum class KernelRelocationType {
    ChainedFixupRebase,         // Apple Chained Fixup (DYLD_CHAINED_PTR_ARM64E / PTR_64)
    Arm64RelocUnsigned,         // ARM64_RELOC_UNSIGNED (64-bit pointer rebase)
    Arm64RelocPage21,           // ARM64_RELOC_PAGE21 (ADRP instruction immediate)
    Arm64RelocPageOff12,        // ARM64_RELOC_PAGEOFF12 (ADD/LDR immediate offset)
    DyldChainedPtrArm64e,       // Authenticated ARM64e pointer with PAC signature
    Unknown
};

// -----------------------------------------------------------------------------
// RelocationStatus: Outcome of KASLR slide and relocation processing
// -----------------------------------------------------------------------------
enum class RelocationStatus {
    Success,
    InvalidSlideAlignment,      // Slide is not aligned to Apple Silicon 16 KB page granule
    SlideOverflow,              // Slide exceeds 512 MB or causes 36-bit VA overflow
    TruncatedMetadata,          // Chained fixups or relocation headers truncated
    UnsupportedFixupFormat,     // Unknown pointer format in fixup chain
    PointerOutOfRange,          // Rebased pointer falls outside kernel address space
    CollisionDetected,          // Slid kernel collides with MMIO or DeviceTree region
    RelocationBoundsExceeded    // Staged kernel exceeds physical DRAM capacity
};

// -----------------------------------------------------------------------------
// AppleRelocationRecord: Diagnostic trace of an individual rebased pointer
// -----------------------------------------------------------------------------
struct AppleRelocationRecord {
    std::uint64_t target_vmaddr{0};
    std::uint64_t target_phys_addr{0};
    std::uint64_t original_value{0};
    std::uint64_t slid_value{0};
    KernelRelocationType type{KernelRelocationType::ChainedFixupRebase};
    std::string segment_name;
};

// -----------------------------------------------------------------------------
// AppleKernelRelocationResult: Detailed outcome of relocation processing
// -----------------------------------------------------------------------------
struct AppleKernelRelocationResult {
    RelocationStatus status{RelocationStatus::Success};
    std::string error_message;
    std::uint64_t applied_slide{0};
    std::uint64_t original_virt_base{0};
    std::uint64_t slid_virt_base{0};
    std::uint64_t original_phys_entry{0};
    std::uint64_t slid_phys_entry{0};
    std::uint64_t original_virt_entry{0};
    std::uint64_t slid_virt_entry{0};
    std::size_t relocations_applied{0};
    std::vector<AppleRelocationRecord> relocation_records;

    [[nodiscard]] bool is_success() const noexcept {
        return status == RelocationStatus::Success;
    }
};

// -----------------------------------------------------------------------------
// Chained Fixups On-Disk Formats (mach-o/fixup-chains.h)
// -----------------------------------------------------------------------------
constexpr std::uint32_t kLoadCmdDyldChainedFixups = 0x80000034u; // LC_DYLD_CHAINED_FIXUPS

#pragma pack(push, 1)
struct DyldChainedFixupsHeader {
    std::uint32_t fixups_version;   // 0
    std::uint32_t starts_offset;    // offset to dyld_chained_starts_in_image
    std::uint32_t imports_offset;   // offset to imports
    std::uint32_t symbols_offset;   // offset to symbol strings
    std::uint32_t imports_count;    // number of imported symbols
    std::uint32_t imports_format;   // DYLD_CHAINED_IMPORT*
    std::uint32_t symbols_format;   // 0 => uncompressed, 1 => zlib
};

struct DyldChainedStartsInImage {
    std::uint32_t seg_count;
    std::uint32_t seg_info_offset[1];
};

struct DyldChainedStartsInSegment {
    std::uint32_t size;
    std::uint16_t page_size;        // 0x4000 for 16 KB pages
    std::uint16_t pointer_format;   // DYLD_CHAINED_PTR_*
    std::uint64_t segment_offset;
    std::uint32_t max_valid_pointer;
    std::uint16_t page_count;
    std::uint16_t page_start[1];
};

struct MachORelocationInfo {
    std::int32_t  r_address;
    std::uint32_t r_symbolnum : 24,
                  r_pcrel     : 1,
                  r_length    : 2,
                  r_extern    : 1,
                  r_type      : 4;
};
#pragma pack(pop)

constexpr std::uint16_t kDyldChainedPtrStartNone = 0xFFFFu;

// -----------------------------------------------------------------------------
// AppleKernelRelocator: Handles KASLR slide and dynamic rebasing for iOS XNU
// -----------------------------------------------------------------------------
class AppleKernelRelocator {
public:
    AppleKernelRelocator() = default;
    ~AppleKernelRelocator() = default;

    // Validate KASLR slide against Apple Silicon architectural rules
    [[nodiscard]] static RelocationStatus validate_slide(
        std::uint64_t slide,
        std::uint64_t kernel_vmsize,
        std::string& out_error);

    // Apply KASLR slide and rebase kernel pointers in guest physical RAM
    [[nodiscard]] static AppleKernelRelocationResult relocate_kernel(
        Memory& guest_mem,
        AppleKernelLoadResult& kernel_load,
        std::uint64_t kaslr_slide,
        const memory::ApplePhysicalMemoryMap& memory_map);

    // Parse and apply Apple Chained Fixups from LC_DYLD_CHAINED_FIXUPS data
    [[nodiscard]] static bool apply_chained_fixups(
        Memory& guest_mem,
        const std::uint8_t* fixup_data,
        std::size_t fixup_size,
        std::uint64_t kaslr_slide,
        AppleKernelLoadResult& kernel_load,
        AppleKernelRelocationResult& out_result);

    // Formatted inspection markdown report
    [[nodiscard]] static std::string generate_relocation_report(
        const AppleKernelRelocationResult& result);

    [[nodiscard]] static const char* status_to_string(RelocationStatus status) noexcept;
    [[nodiscard]] static const char* reloc_type_to_string(KernelRelocationType type) noexcept;
};

} // namespace winios::loader
