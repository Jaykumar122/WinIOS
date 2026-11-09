#include "loader/macho_loader.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace winios::loader {

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
        std::uint32_t nfat = is_be ? swap32(fat_hdr->nfat_arch) : fat_hdr->nfat_arch;

        std::size_t expected_size = sizeof(MachOFatHeader) + nfat * sizeof(MachOFatArch);
        if (size < expected_size) {
            return false;
        }

        const auto* archs = reinterpret_cast<const MachOFatArch*>(
            reinterpret_cast<const std::uint8_t*>(data) + sizeof(MachOFatHeader));

        for (std::uint32_t i = 0; i < nfat; ++i) {
            std::uint32_t cputype = is_be ? swap32(archs[i].cputype) : archs[i].cputype;
            std::uint32_t offset  = is_be ? swap32(archs[i].offset)  : archs[i].offset;
            std::uint32_t arch_sz = is_be ? swap32(archs[i].size)    : archs[i].size;

            if (cputype == kCpuTypeARM64) {
                if (offset + arch_sz > size) {
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

    out_result.header = *hdr;
    out_result.segments.clear();
    out_result.entry_point = 0;
    out_result.initial_sp = 0;
    out_result.lowest_vmaddr = 0xFFFFFFFFFFFFFFFFULL;
    out_result.highest_vmaddr = 0;

    std::size_t cmd_offset = sizeof(MachOHeader64);
    if (cmd_offset + hdr->sizeofcmds > slice_sz) {
        out_result.status = MachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "Load commands span outside Mach-O slice bounds";
        return false;
    }

    for (std::uint32_t i = 0; i < hdr->ncmds; ++i) {
        if (cmd_offset + sizeof(MachOLoadCommand) > slice_sz) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Truncated load command header";
            return false;
        }

        const auto* cmd = reinterpret_cast<const MachOLoadCommand*>(slice_bytes + cmd_offset);
        if (cmd->cmdsize < sizeof(MachOLoadCommand) || cmd_offset + cmd->cmdsize > slice_sz) {
            out_result.status = MachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Corrupted load command size";
            return false;
        }

        if (cmd->cmd == kLoadCmdSegment64) {
            if (cmd->cmdsize < sizeof(MachOSegmentCommand64)) {
                out_result.status = MachOLoadStatus::CorruptedLoadCommands;
                out_result.error_message = "LC_SEGMENT_64 command too small";
                return false;
            }

            const auto* seg_cmd = reinterpret_cast<const MachOSegmentCommand64*>(slice_bytes + cmd_offset);
            MachOSegmentInfo seg;
            seg.name = sanitize_segment_name(seg_cmd->segname);
            seg.vmaddr = seg_cmd->vmaddr;
            seg.vmsize = seg_cmd->vmsize;
            seg.fileoff = seg_cmd->fileoff;
            seg.filesize = seg_cmd->filesize;
            seg.initprot = seg_cmd->initprot;
            seg.maxprot = seg_cmd->maxprot;

            if (seg.vmsize > 0) {
                out_result.lowest_vmaddr = std::min(out_result.lowest_vmaddr, seg.vmaddr);
                out_result.highest_vmaddr = std::max(out_result.highest_vmaddr, seg.vmaddr + seg.vmsize);
                out_result.segments.push_back(seg);
            }
        } else if (cmd->cmd == kLoadCmdUnixThread || cmd->cmd == kLoadCmdThread) {
            // ARM64 Thread State parsing
            // Offset after command header: flavor (4), count (4), state (ARM_THREAD_STATE64_COUNT * 4)
            const auto* p = slice_bytes + cmd_offset + sizeof(MachOLoadCommand);
            const auto* end_cmd = slice_bytes + cmd_offset + cmd->cmdsize;

            while (p + 8 <= end_cmd) {
                std::uint32_t flavor = *reinterpret_cast<const std::uint32_t*>(p);
                std::uint32_t count  = *reinterpret_cast<const std::uint32_t*>(p + 4);
                p += 8;

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
                // Entrypoint is relative to first segment
                if (out_result.entry_point == 0 && !out_result.segments.empty()) {
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

    // Fallback: If entry_point wasn't set by LC_UNIXTHREAD or LC_MAIN,
    // locate first executable segment (__TEXT_EXEC or __TEXT) or first segment.
    if (out_result.entry_point == 0) {
        for (const auto& seg : out_result.segments) {
            if ((seg.initprot & kVmProtExecute) != 0 || seg.name == "__TEXT_EXEC" || seg.name == "__TEXT") {
                out_result.entry_point = seg.vmaddr;
                break;
            }
        }
        if (out_result.entry_point == 0 && !out_result.segments.empty()) {
            out_result.entry_point = out_result.segments.front().vmaddr;
        }
    }

    if (out_result.segments.empty()) {
        out_result.status = MachOLoadStatus::NoSegmentsFound;
        out_result.error_message = "Mach-O contains no valid 64-bit segments";
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

    std::size_t slice_off = 0;
    std::size_t slice_sz = 0;
    (void)locate_arm64_slice(data, size, slice_off, slice_sz);
    const auto* slice_bytes = reinterpret_cast<const std::uint8_t*>(data) + slice_off;

    const std::uint64_t lowest_vm = result.lowest_vmaddr;

    for (auto& seg : result.segments) {
        if (seg.vmsize == 0) continue;

        // Compute physical destination address in guest RAM
        const std::uint64_t vm_offset = seg.vmaddr - lowest_vm;
        const std::uint64_t dest_phys = physical_base + vm_offset + kaslr_slide;

        if (dest_phys < physical_base || (dest_phys + seg.vmsize) > (physical_base + ram_size)) {
            result.status = MachOLoadStatus::MemoryBoundsExceeded;
            result.error_message = "Segment '" + seg.name + "' extends outside allocated guest RAM bounds";
            return result;
        }

        if (seg.fileoff + seg.filesize > slice_sz) {
            result.status = MachOLoadStatus::CorruptedLoadCommands;
            result.error_message = "Segment '" + seg.name + "' file payload exceeds slice boundaries";
            return result;
        }

        // Compute offset within flat guest_mem buffer
        const std::size_t mem_offset = (dest_phys >= physical_base && guest_mem.size() <= dest_phys)
                                           ? static_cast<std::size_t>(dest_phys - physical_base)
                                           : static_cast<std::size_t>(dest_phys);

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
