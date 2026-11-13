#include "loader/macho_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace winios::loader {

const char* macho_load_status_string(MachOLoadStatus s) noexcept {
    switch (s) {
        case MachOLoadStatus::Success:               return "Success";
        case MachOLoadStatus::FileNotFound:          return "FileNotFound";
        case MachOLoadStatus::FileReadError:         return "FileReadError";
        case MachOLoadStatus::InvalidMagic:          return "InvalidMagic";
        case MachOLoadStatus::UnsupportedCpuType:    return "UnsupportedCpuType";
        case MachOLoadStatus::TruncatedHeader:       return "TruncatedHeader";
        case MachOLoadStatus::CorruptedLoadCommands: return "CorruptedLoadCommands";
        case MachOLoadStatus::NoSegmentsFound:       return "NoSegmentsFound";
        case MachOLoadStatus::MemoryBoundsExceeded:  return "MemoryBoundsExceeded";
        case MachOLoadStatus::MemoryWriteFailed:     return "MemoryWriteFailed";
        case MachOLoadStatus::SegmentOverlap:        return "SegmentOverlap";
        case MachOLoadStatus::InvalidEntryPoint:     return "InvalidEntryPoint";
        case MachOLoadStatus::IntegerOverflow:       return "IntegerOverflow";
    }
    return "Unknown";
}

namespace {

inline std::uint32_t swap32(std::uint32_t val) noexcept {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x000000FFu) << 24);
}

inline std::string sanitize_segment_name(const char* name) {
    char buf[17] = {0};
    std::memcpy(buf, name, 16);
    return std::string(buf);
}

} // anonymous namespace

bool MachOLoader::locate_arm64_slice(
    const void* data,
    std::size_t size,
    std::size_t& out_slice_offset,
    std::size_t& out_slice_size) {

    if (!data || size < sizeof(std::uint32_t)) {
        return false;
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, data, sizeof(magic));

    // 1. Direct Thin 64-bit ARM64 Mach-O
    if (magic == kMachOMagic64) {
        out_slice_offset = 0;
        out_slice_size = size;
        return true;
    }

    // 2. Fat / Universal Mach-O (Big-Endian or Little-Endian)
    if (magic == kMachOFatMagic || magic == kMachOFatCigam) {
        const bool is_be = (magic == kMachOFatMagic);
        if (size < sizeof(MachOFatHeader)) {
            return false;
        }

        const auto* fat_hdr = reinterpret_cast<const MachOFatHeader*>(data);
        const std::uint32_t nfat = is_be ? swap32(fat_hdr->nfat_arch) : fat_hdr->nfat_arch;

        if (nfat == 0 || nfat > 1024u) {
            return false;
        }

        const std::size_t arch_bytes = static_cast<std::size_t>(nfat) * sizeof(MachOFatArch);
        if (sizeof(MachOFatHeader) > size || arch_bytes > (size - sizeof(MachOFatHeader))) {
            return false;
        }

        const auto* archs = reinterpret_cast<const MachOFatArch*>(
            reinterpret_cast<const std::uint8_t*>(data) + sizeof(MachOFatHeader));

        for (std::uint32_t i = 0; i < nfat; ++i) {
            const std::uint32_t cputype = is_be ? swap32(archs[i].cputype) : archs[i].cputype;
            const std::uint32_t offset  = is_be ? swap32(archs[i].offset)  : archs[i].offset;
            const std::uint32_t arch_sz = is_be ? swap32(archs[i].size)    : archs[i].size;

            if (cputype == kCpuTypeARM64) {
                if (offset > size || arch_sz > (size - offset) || arch_sz < sizeof(MachOHeader64)) {
                    return false;
                }
                out_slice_offset = offset;
                out_slice_size = arch_sz;
                return true;
            }
        }
    }

    return false;
}

bool MachOLoader::parse_metadata(
    const void* data,
    std::size_t size,
    MachOLoadResult& out_result) {

    std::size_t slice_off = 0;
    std::size_t slice_sz = 0;
    if (!locate_arm64_slice(data, size, slice_off, slice_sz)) {
        out_result.status = MachOLoadStatus::InvalidMagic;
        out_result.error_message = "File is not a valid ARM64 Mach-O or Universal binary";
        return false;
    }

    if (slice_sz < sizeof(MachOHeader64)) {
        out_result.status = MachOLoadStatus::TruncatedHeader;
        out_result.error_message = "Mach-O header truncated";
        return false;
    }

    const auto* slice_bytes = reinterpret_cast<const std::uint8_t*>(data) + slice_off;
    const auto* hdr = reinterpret_cast<const MachOHeader64*>(slice_bytes);

    if (hdr->magic != kMachOMagic64) {
        out_result.status = MachOLoadStatus::InvalidMagic;
        out_result.error_message = "Mach-O slice magic is not MH_MAGIC_64";
        return false;
    }

    if (hdr->cputype != kCpuTypeARM64) {
        out_result.status = MachOLoadStatus::UnsupportedCpuType;
        out_result.error_message = "Mach-O CPU type is not ARM64";
        return false;
    }

    if (hdr->ncmds == 0 || hdr->ncmds > 4096u) {
        out_result.status = MachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "Invalid or oversized Mach-O load command count";
        return false;
    }

    if (sizeof(MachOHeader64) > slice_sz || hdr->sizeofcmds > (slice_sz - sizeof(MachOHeader64))) {
        out_result.status = MachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "Load commands span outside Mach-O slice bounds";
        return false;
    }

    const std::uint64_t min_required_cmds_size = static_cast<std::uint64_t>(hdr->ncmds) * sizeof(MachOLoadCommand);
    if (hdr->sizeofcmds < min_required_cmds_size) {
        out_result.status = MachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "Mach-O sizeofcmds is smaller than minimum required for ncmds";
        return false;
    }

    out_result.header = *hdr;
    out_result.segments.clear();
    out_result.fileset_entries.clear();
    out_result.entry_point = 0;
    out_result.initial_sp = 0;
    out_result.initial_lr = 0;
    out_result.initial_cpsr = 0;
    out_result.lowest_vmaddr = 0xFFFFFFFFFFFFFFFFULL;
    out_result.highest_vmaddr = 0;

    std::size_t cmd_offset = sizeof(MachOHeader64);
    std::size_t cumulative_cmd_size = 0;

    for (std::uint32_t i = 0; i < hdr->ncmds; ++i) {
        if (cmd_offset > slice_sz || sizeof(MachOLoadCommand) > (slice_sz - cmd_offset)) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Truncated load command header";
            return false;
        }

        const auto* cmd = reinterpret_cast<const MachOLoadCommand*>(slice_bytes + cmd_offset);
        if (cmd->cmdsize < sizeof(MachOLoadCommand) || (cmd->cmdsize % 4) != 0) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Corrupted load command size or misalignment";
            return false;
        }

        if (cmd->cmdsize > (slice_sz - cmd_offset)) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Load command exceeds slice boundaries";
            return false;
        }

        if (cmd->cmdsize > (hdr->sizeofcmds - cumulative_cmd_size)) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Cumulative command sizes exceed header sizeofcmds";
            return false;
        }
        cumulative_cmd_size += cmd->cmdsize;

        if (cmd->cmd == kLoadCmdSegment64) {
            if (cmd->cmdsize < sizeof(MachOSegmentCommand64)) {
                out_result.status = MachOLoadStatus::CorruptedLoadCommands;
                out_result.error_message = "LC_SEGMENT_64 command too small";
                return false;
            }

            const auto* seg_cmd = reinterpret_cast<const MachOSegmentCommand64*>(slice_bytes + cmd_offset);
            const std::size_t nsects_bytes = static_cast<std::size_t>(seg_cmd->nsects) * sizeof(MachOSection64);
            if (seg_cmd->nsects > 1024u || (sizeof(MachOSegmentCommand64) + nsects_bytes) > cmd->cmdsize) {
                out_result.status = MachOLoadStatus::CorruptedLoadCommands;
                out_result.error_message = "LC_SEGMENT_64 section table exceeds command size";
                return false;
            }

            MachOSegmentInfo seg;
            seg.name = sanitize_segment_name(seg_cmd->segname);
            seg.vmaddr = seg_cmd->vmaddr;
            seg.vmsize = seg_cmd->vmsize;
            seg.fileoff = seg_cmd->fileoff;
            seg.filesize = seg_cmd->filesize;
            seg.initprot = seg_cmd->initprot;
            seg.maxprot = seg_cmd->maxprot;

            if (seg.vmsize > 0) {
                if (seg.vmaddr > (0xFFFFFFFFFFFFFFFFULL - seg.vmsize)) {
                    out_result.status = MachOLoadStatus::IntegerOverflow;
                    out_result.error_message = "Segment '" + seg.name + "' virtual memory range wraps 64-bit address space";
                    return false;
                }

                if (seg.filesize > seg.vmsize) {
                    out_result.status = MachOLoadStatus::CorruptedLoadCommands;
                    out_result.error_message = "Segment '" + seg.name + "' filesize exceeds vmsize";
                    return false;
                }

                if (seg.fileoff > slice_sz || seg.filesize > (slice_sz - seg.fileoff)) {
                    out_result.status = MachOLoadStatus::CorruptedLoadCommands;
                    out_result.error_message = "Segment '" + seg.name + "' file payload exceeds slice boundaries";
                    return false;
                }

                out_result.lowest_vmaddr = std::min(out_result.lowest_vmaddr, seg.vmaddr);
                out_result.highest_vmaddr = std::max(out_result.highest_vmaddr, seg.vmaddr + seg.vmsize);
                out_result.segments.push_back(seg);
            }
        } else if (cmd->cmd == kLoadCmdUnixThread || cmd->cmd == kLoadCmdThread) {
            const auto* p = slice_bytes + cmd_offset + sizeof(MachOLoadCommand);
            const auto* end_cmd = slice_bytes + cmd_offset + cmd->cmdsize;

            while (p + 8 <= end_cmd) {
                const std::uint32_t flavor = *reinterpret_cast<const std::uint32_t*>(p);
                const std::uint32_t count  = *reinterpret_cast<const std::uint32_t*>(p + 4);
                p += 8;

                if (count > 1024u || p + static_cast<std::size_t>(count) * 4 > end_cmd) {
                    break;
                }

                if (flavor == kArmThreadState64 && count >= sizeof(MachOArmThreadState64) / 4) {
                    if (p + sizeof(MachOArmThreadState64) <= end_cmd) {
                        const auto* ts = reinterpret_cast<const MachOArmThreadState64*>(p);
                        out_result.entry_point = ts->pc;
                        out_result.initial_sp = ts->sp;
                        out_result.initial_lr = ts->lr;
                        out_result.initial_cpsr = ts->cpsr;
                    }
                    break;
                }
                p += static_cast<std::size_t>(count) * 4;
            }
        } else if (cmd->cmd == kLoadCmdMain) {
            if (cmd->cmdsize >= sizeof(MachOEntrypointCommand)) {
                const auto* ep = reinterpret_cast<const MachOEntrypointCommand*>(slice_bytes + cmd_offset);
                if (out_result.entry_point == 0 && !out_result.segments.empty()) {
                    if (ep->entryoff > (0xFFFFFFFFFFFFFFFFULL - out_result.segments[0].vmaddr)) {
                        out_result.status = MachOLoadStatus::IntegerOverflow;
                        out_result.error_message = "LC_MAIN entryoff causes 64-bit integer overflow";
                        return false;
                    }
                    out_result.entry_point = out_result.segments[0].vmaddr + ep->entryoff;
                }
                if (out_result.initial_sp == 0 && ep->stacksize > 0) {
                    out_result.initial_sp = ep->stacksize;
                }
            }
        } else if (cmd->cmd == kLoadCmdFilesetEntry) {
            if (cmd->cmdsize >= sizeof(MachOFilesetEntryCommand)) {
                const auto* fse = reinterpret_cast<const MachOFilesetEntryCommand*>(slice_bytes + cmd_offset);
                MachOFilesetEntryInfo entry_info;
                entry_info.vmaddr = fse->vmaddr;
                entry_info.fileoff = fse->fileoff;
                if (fse->entry_id_offset < cmd->cmdsize && (cmd_offset + fse->entry_id_offset) < slice_sz) {
                    const char* str = reinterpret_cast<const char*>(slice_bytes + cmd_offset + fse->entry_id_offset);
                    const std::size_t max_len = slice_sz - (cmd_offset + fse->entry_id_offset);
                    std::size_t actual_len = 0;
                    while (actual_len < max_len && str[actual_len] != '\0') {
                        actual_len++;
                    }
                    entry_info.entry_id = std::string(str, actual_len);
                }
                out_result.fileset_entries.push_back(std::move(entry_info));
            }
        }

        cmd_offset += cmd->cmdsize;
    }

    if (cumulative_cmd_size != hdr->sizeofcmds) {
        out_result.status = MachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "Cumulative command sizes do not match header sizeofcmds";
        return false;
    }

    if (out_result.segments.empty()) {
        out_result.status = MachOLoadStatus::NoSegmentsFound;
        out_result.error_message = "Mach-O contains no valid 64-bit segments";
        return false;
    }

    // Segment Overlap Verification
    for (std::size_t i = 0; i < out_result.segments.size(); ++i) {
        const auto& seg_a = out_result.segments[i];
        if (seg_a.vmsize == 0) continue;
        const std::uint64_t a_start = seg_a.vmaddr;
        const std::uint64_t a_end = seg_a.vmaddr + seg_a.vmsize;

        for (std::size_t j = i + 1; j < out_result.segments.size(); ++j) {
            const auto& seg_b = out_result.segments[j];
            if (seg_b.vmsize == 0) continue;
            const std::uint64_t b_start = seg_b.vmaddr;
            const std::uint64_t b_end = seg_b.vmaddr + seg_b.vmsize;

            if (std::max(a_start, b_start) < std::min(a_end, b_end)) {
                out_result.status = MachOLoadStatus::SegmentOverlap;
                out_result.error_message = "Segments '" + seg_a.name + "' and '" + seg_b.name + "' overlap in virtual memory";
                return false;
            }
        }
    }

    // Fallback Entry Point Resolution
    if (out_result.entry_point == 0) {
        for (const auto& seg : out_result.segments) {
            if ((seg.initprot & kVmProtExecute) != 0 || seg.name == "__TEXT_EXEC" || seg.name == "__TEXT") {
                out_result.entry_point = seg.vmaddr;
                break;
            }
        }
        if (out_result.entry_point == 0 && !out_result.segments.empty() && hdr->filetype != kMachOFileTypeDylib && hdr->filetype != kMachOFileTypeFileset) {
            out_result.entry_point = out_result.segments.front().vmaddr;
        }
    }

    // Entry Point Executability Verification
    if (out_result.entry_point != 0) {
        bool entry_in_exec = false;
        for (const auto& seg : out_result.segments) {
            if ((seg.initprot & kVmProtExecute) != 0 || (seg.maxprot & kVmProtExecute) != 0) {
                if (out_result.entry_point >= seg.vmaddr &&
                    out_result.entry_point < (seg.vmaddr + seg.vmsize)) {
                    entry_in_exec = true;
                    break;
                }
            }
        }
        if (!entry_in_exec) {
            out_result.status = MachOLoadStatus::InvalidEntryPoint;
            std::ostringstream ep_ss;
            ep_ss << "0x" << std::hex << out_result.entry_point;
            out_result.error_message = "Mach-O entry point " + ep_ss.str() + " does not lie within any executable segment";
            return false;
        }
    } else if (hdr->filetype == kMachOFileTypeExecute) {
        out_result.status = MachOLoadStatus::InvalidEntryPoint;
        out_result.error_message = "Executable Mach-O has no valid entry point";
        return false;
    }

    out_result.total_vmsize = out_result.highest_vmaddr - out_result.lowest_vmaddr;
    out_result.status = MachOLoadStatus::Success;
    return true;
}

MachOLoadResult MachOLoader::load_from_buffer(
    const void* data,
    std::size_t size,
    Memory& guest_mem,
    std::uint64_t physical_base,
    std::uint64_t ram_size,
    std::uint64_t kaslr_slide,
    GuestMemoryMap* memory_map) {

    MachOLoadResult result{};
    if (!parse_metadata(data, size, result)) {
        return result;
    }

    if (ram_size == 0) {
        result.status = MachOLoadStatus::MemoryBoundsExceeded;
        result.error_message = "Allocated guest RAM size is zero";
        return result;
    }

    if (physical_base > (0xFFFFFFFFFFFFFFFFULL - ram_size)) {
        result.status = MachOLoadStatus::IntegerOverflow;
        result.error_message = "Guest physical RAM range wraps 64-bit address space";
        return result;
    }

    std::size_t slice_off = 0;
    std::size_t slice_sz = 0;
    (void)locate_arm64_slice(data, size, slice_off, slice_sz);
    const auto* slice_bytes = reinterpret_cast<const std::uint8_t*>(data) + slice_off;

    const std::uint64_t lowest_vm = result.lowest_vmaddr;

    for (auto& seg : result.segments) {
        if (seg.vmsize == 0) continue;

        if (seg.vmaddr < lowest_vm) {
            result.status = MachOLoadStatus::CorruptedLoadCommands;
            result.error_message = "Segment vmaddr is less than lowest_vmaddr";
            return result;
        }

        const std::uint64_t vm_offset = seg.vmaddr - lowest_vm;
        if (vm_offset > (0xFFFFFFFFFFFFFFFFULL - physical_base - kaslr_slide)) {
            result.status = MachOLoadStatus::IntegerOverflow;
            result.error_message = "Segment address calculation overflow";
            return result;
        }

        const std::uint64_t dest_phys = physical_base + vm_offset + kaslr_slide;
        if (dest_phys > (0xFFFFFFFFFFFFFFFFULL - seg.vmsize)) {
            result.status = MachOLoadStatus::IntegerOverflow;
            result.error_message = "Segment destination physical range overflow";
            return result;
        }

        if (dest_phys < physical_base || (dest_phys + seg.vmsize) > (physical_base + ram_size)) {
            result.status = MachOLoadStatus::MemoryBoundsExceeded;
            result.error_message = "Segment '" + seg.name + "' extends outside allocated guest RAM bounds";
            return result;
        }

        if (seg.fileoff > slice_sz || seg.filesize > (slice_sz - seg.fileoff)) {
            result.status = MachOLoadStatus::CorruptedLoadCommands;
            result.error_message = "Segment '" + seg.name + "' file payload exceeds slice boundaries";
            return result;
        }

        // Compute offset within flat guest_mem buffer
        const std::size_t mem_offset = (dest_phys >= physical_base && guest_mem.size() <= dest_phys)
                                           ? static_cast<std::size_t>(dest_phys - physical_base)
                                           : static_cast<std::size_t>(dest_phys);

        if (mem_offset > guest_mem.size() || seg.vmsize > (guest_mem.size() - mem_offset)) {
            result.status = MachOLoadStatus::MemoryBoundsExceeded;
            result.error_message = "Segment '" + seg.name + "' exceeds guest memory size";
            return result;
        }

        // Copy file bytes into guest RAM
        if (seg.filesize > 0) {
            const auto* src = slice_bytes + seg.fileoff;
            for (std::size_t b = 0; b < seg.filesize; ++b) {
                guest_mem.write_u8(mem_offset + b, src[b]);
            }
        }

        // Zero-fill remaining virtual size (BSS)
        if (seg.vmsize > seg.filesize) {
            const std::size_t bss_size = static_cast<std::size_t>(seg.vmsize - seg.filesize);
            const std::size_t bss_start = mem_offset + static_cast<std::size_t>(seg.filesize);
            for (std::size_t b = 0; b < bss_size; ++b) {
                guest_mem.write_u8(bss_start + b, 0);
            }
        }

        seg.mapped_address = dest_phys;

        // Register in guest memory map if provided
        if (memory_map) {
            memory_map->register_reserved("MachO_" + seg.name, dest_phys, seg.vmsize);
        }
    }

    // Adjust entry point if it was virtual high-half address
    if (result.entry_point >= lowest_vm && result.entry_point < result.highest_vmaddr) {
        result.entry_point = physical_base + (result.entry_point - lowest_vm) + kaslr_slide;
    }

    result.status = MachOLoadStatus::Success;
    return result;
}

MachOLoadResult MachOLoader::load_from_file(
    const std::string& path,
    Memory& guest_mem,
    std::uint64_t physical_base,
    std::uint64_t ram_size,
    std::uint64_t kaslr_slide,
    GuestMemoryMap* memory_map) {

    MachOLoadResult result{};
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.status = MachOLoadStatus::FileNotFound;
        result.error_message = "Cannot open Mach-O binary: " + path;
        return result;
    }

    std::streamsize sz = file.tellg();
    if (sz <= 0) {
        result.status = MachOLoadStatus::TruncatedHeader;
        result.error_message = "Mach-O file is empty: " + path;
        return result;
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(sz));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), sz)) {
        result.status = MachOLoadStatus::FileReadError;
        result.error_message = "Failed reading Mach-O binary: " + path;
        return result;
    }

    return load_from_buffer(buffer.data(), buffer.size(), guest_mem,
                            physical_base, ram_size, kaslr_slide, memory_map);
}

} // namespace winios::loader
