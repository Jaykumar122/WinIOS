#include "loader/kernel_image_parser.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#include "loader/userspace_macho_loader.h"

namespace winios::loader {

const char* kernel_image_parse_status_string(KernelImageParseStatus s) noexcept {
    switch (s) {
        case KernelImageParseStatus::Success:               return "Success";
        case KernelImageParseStatus::InvalidMagic:          return "InvalidMagic";
        case KernelImageParseStatus::UnsupportedCpuType:    return "UnsupportedCpuType";
        case KernelImageParseStatus::UnsupportedCpuSubtype: return "UnsupportedCpuSubtype";
        case KernelImageParseStatus::UnsupportedFileType:   return "UnsupportedFileType";
        case KernelImageParseStatus::TruncatedHeader:       return "TruncatedHeader";
        case KernelImageParseStatus::CorruptedLoadCommands: return "CorruptedLoadCommands";
        case KernelImageParseStatus::SegmentOutOfBounds:    return "SegmentOutOfBounds";
        case KernelImageParseStatus::OverlappingSegments:   return "OverlappingSegments";
        case KernelImageParseStatus::MisalignedSegment:     return "MisalignedSegment";
        case KernelImageParseStatus::IntegerOverflow:       return "IntegerOverflow";
        case KernelImageParseStatus::MissingEntryPoint:     return "MissingEntryPoint";
        case KernelImageParseStatus::BufferTooSmall:        return "BufferTooSmall";
        default:                                            return "Unknown";
    }
}

const KernelSegmentInfo* KernelParsedImage::find_segment(std::string_view name) const noexcept {
    for (const auto& seg : segments) {
        if (seg.name == name) {
            return &seg;
        }
    }
    return nullptr;
}

namespace {

inline std::string sanitize_segname(const char* name) {
    char buf[17] = {0};
    std::memcpy(buf, name, 16);
    return std::string(buf);
}

// ARM64 Thread State Flavor in Darwin (ARM_THREAD_STATE64)
constexpr std::uint32_t kDarwinArmThreadState64Flavor = 6u;
constexpr std::uint32_t kLegacyArmThreadState64Flavor = 1u;

} // anonymous namespace

bool KernelImageParser::extract_thread_state_entry(
    const std::uint8_t* cmd_data,
    std::size_t cmd_size,
    std::uint64_t& out_entry) {

    // LC_UNIXTHREAD payload:
    // uint32_t cmd;
    // uint32_t cmdsize;
    // uint32_t flavor;
    // uint32_t count;
    // struct arm_thread_state64:
    //   uint64_t x[29];
    //   uint64_t fp;
    //   uint64_t lr;
    //   uint64_t sp;
    //   uint64_t pc;     // offset: 16 (cmd header) + 32 * 8 = 272
    //   uint32_t cpsr;
    //   uint32_t pad;
    constexpr std::size_t kMinHeaderSize = 16;
    if (cmd_size < kMinHeaderSize + 264) {
        return false;
    }

    std::uint32_t flavor = 0;
    std::memcpy(&flavor, cmd_data + 8, sizeof(flavor));

    if (flavor != kDarwinArmThreadState64Flavor && flavor != kLegacyArmThreadState64Flavor) {
        // Try direct PC read at standard offset 272 if size permits
        if (cmd_size >= 280) {
            std::memcpy(&out_entry, cmd_data + 272, sizeof(out_entry));
            return out_entry != 0;
        }
        return false;
    }

    // Offset of PC in arm_thread_state64 is x[29] (232), fp (240), lr (248), sp (256), pc (264) from state start
    // Header is 16 bytes, so pc is at 16 + 256 = 272
    if (cmd_size >= 280) {
        std::memcpy(&out_entry, cmd_data + 272, sizeof(out_entry));
        return true;
    }
    return false;
}

bool KernelImageParser::validate_segments(
    const std::vector<KernelSegmentInfo>& segments,
    std::size_t raw_image_size,
    std::uint64_t page_granule,
    std::string& out_error) {

    if (segments.empty()) {
        out_error = "Kernel image contains zero segments";
        return false;
    }

    for (const auto& seg : segments) {
        if (page_granule > 0 && (seg.vmaddr % page_granule) != 0) {
            out_error = "Segment '" + seg.name + "' vmaddr 0x" +
                        std::to_string(seg.vmaddr) + " is not aligned to granule " +
                        std::to_string(page_granule);
            return false;
        }

        // Integer overflow check
        if (seg.vmaddr + seg.vmsize < seg.vmaddr) {
            out_error = "Segment '" + seg.name + "' virtual address range overflows 64-bit integer";
            return false;
        }

        if (seg.filesize > 0) {
            if (seg.fileoff + seg.filesize < seg.fileoff) {
                out_error = "Segment '" + seg.name + "' file offset range overflows 64-bit integer";
                return false;
            }
            if (seg.fileoff + seg.filesize > raw_image_size) {
                out_error = "Segment '" + seg.name + "' file range [0x" +
                            std::to_string(seg.fileoff) + ", 0x" +
                            std::to_string(seg.fileoff + seg.filesize) +
                            "] exceeds raw image size " + std::to_string(raw_image_size);
                return false;
            }
        }
    }

    // Check for overlapping virtual segments (excluding zero-sized segments)
    std::vector<KernelSegmentInfo> sorted = segments;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.vmaddr < b.vmaddr;
    });

    for (std::size_t i = 1; i < sorted.size(); ++i) {
        const auto& prev = sorted[i - 1];
        const auto& curr = sorted[i];

        if (prev.vmsize == 0 || curr.vmsize == 0) {
            continue;
        }

        if (prev.vmaddr + prev.vmsize > curr.vmaddr) {
            out_error = "Segment '" + curr.name + "' [0x" + std::to_string(curr.vmaddr) +
                        "] overlaps with segment '" + prev.name + "' [0x" +
                        std::to_string(prev.vmaddr) + " - 0x" +
                        std::to_string(prev.vmaddr + prev.vmsize) + "]";
            return false;
        }
    }

    return true;
}

KernelImageParseStatus KernelImageParser::parse(
    const std::uint8_t* data,
    std::size_t size,
    KernelParsedImage& out_image) {

    out_image = KernelParsedImage{};

    if (!data || size < sizeof(MachOHeader64Raw)) {
        out_image.error_message = "Buffer too small for Mach-O 64-bit header";
        return KernelImageParseStatus::TruncatedHeader;
    }

    MachOHeader64Raw header;
    std::memcpy(&header, data, sizeof(header));

    if (header.magic != kMachO64Magic) {
        out_image.error_message = "Invalid Mach-O 64-bit magic (expected 0xFEEDFACF, got 0x" +
                                  std::to_string(header.magic) + ")";
        return KernelImageParseStatus::InvalidMagic;
    }

    if (header.cputype != kMachOCpuTypeARM64) {
        out_image.error_message = "Unsupported CPU type (expected ARM64 0x0100000C, got 0x" +
                                  std::to_string(header.cputype) + ")";
        return KernelImageParseStatus::UnsupportedCpuType;
    }

    std::uint32_t subtype = header.cpusubtype & kMachOCpuSubtypeMask;
    if (subtype != kMachOCpuSubtypeArm64All &&
        subtype != kMachOCpuSubtypeArm64V8 &&
        subtype != kMachOCpuSubtypeArm64E) {
        out_image.error_message = "Unsupported ARM64 CPU subtype: " + std::to_string(subtype);
        return KernelImageParseStatus::UnsupportedCpuSubtype;
    }

    if (header.filetype != kMachOFileTypeExecute) {
        out_image.error_message = "Kernel image must be MH_EXECUTE (expected 2, got " +
                                  std::to_string(header.filetype) + ")";
        return KernelImageParseStatus::UnsupportedFileType;
    }

    out_image.format = KernelImageFormat::MachO64;
    out_image.cputype = header.cputype;
    out_image.cpusubtype = header.cpusubtype;

    std::size_t offset = sizeof(MachOHeader64Raw);
    if (offset + header.sizeofcmds > size) {
        out_image.error_message = "Load commands region exceeds file size";
        return KernelImageParseStatus::CorruptedLoadCommands;
    }

    bool has_entry = false;
    std::uint64_t text_vmaddr = 0;
    bool found_text = false;
    std::uint64_t lowest_vmaddr = ~0ULL;
    std::uint64_t highest_vmaddr = 0;

    for (std::uint32_t i = 0; i < header.ncmds; ++i) {
        if (offset + sizeof(MachOLoadCommandRaw) > size) {
            out_image.error_message = "Truncated load command header at command index " + std::to_string(i);
            return KernelImageParseStatus::CorruptedLoadCommands;
        }

        MachOLoadCommandRaw cmd;
        std::memcpy(&cmd, data + offset, sizeof(cmd));

        if (cmd.cmdsize < sizeof(MachOLoadCommandRaw) || offset + cmd.cmdsize > size) {
            out_image.error_message = "Invalid cmdsize " + std::to_string(cmd.cmdsize) +
                                      " at command index " + std::to_string(i);
            return KernelImageParseStatus::CorruptedLoadCommands;
        }

        if (cmd.cmd == kCmdSegment64) {
            if (cmd.cmdsize < sizeof(MachOSegment64Raw)) {
                out_image.error_message = "LC_SEGMENT_64 size too small";
                return KernelImageParseStatus::CorruptedLoadCommands;
            }

            MachOSegment64Raw seg;
            std::memcpy(&seg, data + offset, sizeof(seg));

            KernelSegmentInfo info;
            info.name = sanitize_segname(seg.segname);
            info.vmaddr = seg.vmaddr;
            info.vmsize = seg.vmsize;
            info.fileoff = seg.fileoff;
            info.filesize = seg.filesize;
            info.maxprot = seg.maxprot;
            info.initprot = seg.initprot;
            info.nsects = seg.nsects;
            info.is_executable = (seg.initprot & kProtExecute) != 0;
            info.is_writable = (seg.initprot & kProtWrite) != 0;

            if (info.name == "__TEXT") {
                text_vmaddr = info.vmaddr;
                found_text = true;
            }

            if (info.vmsize > 0) {
                if (info.vmaddr < lowest_vmaddr) {
                    lowest_vmaddr = info.vmaddr;
                }
                if (info.vmaddr + info.vmsize > highest_vmaddr) {
                    highest_vmaddr = info.vmaddr + info.vmsize;
                }
            }

            out_image.segments.push_back(std::move(info));
        } else if (cmd.cmd == kCmdUnixThread) {
            std::uint64_t thread_entry = 0;
            if (extract_thread_state_entry(data + offset, cmd.cmdsize, thread_entry)) {
                out_image.entry_point_vaddr = thread_entry;
                has_entry = true;
            }
        } else if (cmd.cmd == kCmdMain) {
            if (cmd.cmdsize >= 24) {
                std::uint64_t entryoff = 0;
                std::memcpy(&entryoff, data + offset + 8, sizeof(entryoff));
                out_image.entry_point_fileoff = entryoff;
                if (!has_entry) {
                    // Will calculate entry_point_vaddr once __TEXT vmaddr is established
                    out_image.entry_point_vaddr = text_vmaddr + entryoff;
                    has_entry = true;
                }
            }
        }

        offset += cmd.cmdsize;
    }

    if (lowest_vmaddr != ~0ULL) {
        out_image.preferred_base_vaddr = lowest_vmaddr;
        out_image.highest_vaddr = highest_vmaddr;
        out_image.total_vm_size = highest_vmaddr - lowest_vmaddr;
    }

    // Resolve entrypoint if found through LC_MAIN with deferred __TEXT vmaddr
    if (out_image.entry_point_vaddr == 0 && out_image.entry_point_fileoff > 0 && found_text) {
        out_image.entry_point_vaddr = text_vmaddr + out_image.entry_point_fileoff;
        has_entry = true;
    }

    // If entry point is still 0, but __TEXT was found, check if entry defaults to __TEXT start
    if (!has_entry && found_text) {
        out_image.entry_point_vaddr = text_vmaddr;
        has_entry = true;
    }

    if (!has_entry) {
        out_image.error_message = "No valid kernel entry point discovered in load commands";
        return KernelImageParseStatus::MissingEntryPoint;
    }

    // Validate segments
    std::string seg_err;
    if (!validate_segments(out_image.segments, size, kApplePageSize16K, seg_err)) {
        out_image.error_message = seg_err;
        if (seg_err.find("overlaps") != std::string::npos) {
            return KernelImageParseStatus::OverlappingSegments;
        }
        if (seg_err.find("aligned") != std::string::npos) {
            return KernelImageParseStatus::MisalignedSegment;
        }
        if (seg_err.find("exceeds raw image size") != std::string::npos) {
            return KernelImageParseStatus::SegmentOutOfBounds;
        }
        return KernelImageParseStatus::IntegerOverflow;
    }

    return KernelImageParseStatus::Success;
}

} // namespace winios::loader
