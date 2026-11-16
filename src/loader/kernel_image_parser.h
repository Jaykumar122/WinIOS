#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace winios::loader {

// -----------------------------------------------------------------------------
// Kernel Image Format & Status Enumerations
// -----------------------------------------------------------------------------
enum class KernelImageFormat {
    Unknown,
    MachO64,
    RawARM64Binary
};

enum class KernelImageParseStatus {
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
    MissingEntryPoint,
    BufferTooSmall
};

const char* kernel_image_parse_status_string(KernelImageParseStatus s) noexcept;

// -----------------------------------------------------------------------------
// Kernel Segment Metadata
// -----------------------------------------------------------------------------
struct KernelSegmentInfo {
    std::string   name;
    std::uint64_t vmaddr{0};
    std::uint64_t vmsize{0};
    std::uint64_t fileoff{0};
    std::uint64_t filesize{0};
    std::uint32_t maxprot{0};
    std::uint32_t initprot{0};
    std::uint32_t nsects{0};
    bool          is_executable{false};
    bool          is_writable{false};
};

// -----------------------------------------------------------------------------
// Parsed Kernel Image Container
// -----------------------------------------------------------------------------
struct KernelParsedImage {
    KernelImageFormat            format{KernelImageFormat::Unknown};
    std::uint32_t                cputype{0};
    std::uint32_t                cpusubtype{0};
    std::uint64_t                entry_point_vaddr{0};
    std::uint64_t                entry_point_fileoff{0};
    std::uint64_t                preferred_base_vaddr{0};
    std::uint64_t                highest_vaddr{0};
    std::uint64_t                total_vm_size{0};
    std::vector<KernelSegmentInfo> segments;
    std::string                  error_message;

    [[nodiscard]] const KernelSegmentInfo* find_segment(std::string_view name) const noexcept;
};

// -----------------------------------------------------------------------------
// Kernel Image Parser
// -----------------------------------------------------------------------------
class KernelImageParser {
public:
    static KernelImageParseStatus parse(
        const std::uint8_t* data,
        std::size_t size,
        KernelParsedImage& out_image);

    static bool validate_segments(
        const std::vector<KernelSegmentInfo>& segments,
        std::size_t raw_image_size,
        std::uint64_t page_granule,
        std::string& out_error);

    static bool extract_thread_state_entry(
        const std::uint8_t* cmd_data,
        std::size_t cmd_size,
        std::uint64_t& out_entry);
};

} // namespace winios::loader
