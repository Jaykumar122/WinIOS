#include "loader/userspace_macho_loader.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "loader/linux_image_loader.h"

namespace winios::loader {

const char* userspace_macho_status_string(UserspaceMachOLoadStatus s) noexcept {
    switch (s) {
        case UserspaceMachOLoadStatus::Success:               return "Success";
        case UserspaceMachOLoadStatus::InvalidMagic:          return "InvalidMagic";
        case UserspaceMachOLoadStatus::UnsupportedCpuType:    return "UnsupportedCpuType";
        case UserspaceMachOLoadStatus::UnsupportedCpuSubtype: return "UnsupportedCpuSubtype";
        case UserspaceMachOLoadStatus::UnsupportedFileType:   return "UnsupportedFileType";
        case UserspaceMachOLoadStatus::TruncatedHeader:       return "TruncatedHeader";
        case UserspaceMachOLoadStatus::CorruptedLoadCommands: return "CorruptedLoadCommands";
        case UserspaceMachOLoadStatus::SegmentOutOfBounds:    return "SegmentOutOfBounds";
        case UserspaceMachOLoadStatus::OverlappingSegments:   return "OverlappingSegments";
        case UserspaceMachOLoadStatus::MisalignedSegment:     return "MisalignedSegment";
        case UserspaceMachOLoadStatus::IntegerOverflow:       return "IntegerOverflow";
        case UserspaceMachOLoadStatus::NoExecutableSegment:   return "NoExecutableSegment";
        case UserspaceMachOLoadStatus::MissingEntryPoint:     return "MissingEntryPoint";
        case UserspaceMachOLoadStatus::MemoryWriteFailed:     return "MemoryWriteFailed";
        default:                                              return "Unknown";
    }
}

namespace {

inline std::string sanitize_segname(const char* name) {
    char buf[17] = {0};
    std::memcpy(buf, name, 16);
    return std::string(buf);
}

inline std::string format_version(std::uint32_t ver) {
    std::uint32_t major = (ver >> 16) & 0xFFFFu;
    std::uint32_t minor = (ver >> 8) & 0xFFu;
    std::uint32_t patch = ver & 0xFFu;
    std::ostringstream oss;
    oss << major << "." << minor << "." << patch;
    return oss.str();
}

} // anonymous namespace

bool UserspaceMachOLoader::parse_metadata(
    const void* data,
    std::size_t size,
    UserspaceLoadResult& out_result) {

    out_result = UserspaceLoadResult{};

    if (!data || size < sizeof(MachOHeader64Raw)) {
        out_result.status = UserspaceMachOLoadStatus::TruncatedHeader;
        out_result.error_message = "File is truncated or smaller than 64-bit Mach-O header";
        return false;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::memcpy(&out_result.header, bytes, sizeof(MachOHeader64Raw));
    const auto& hdr = out_result.header;

    // 1. Verify Magic
    if (hdr.magic != kMachO64Magic) {
        out_result.status = UserspaceMachOLoadStatus::InvalidMagic;
        out_result.error_message = "Invalid Mach-O 64-bit magic (expected MH_MAGIC_64 0xFEEDFACF)";
        return false;
    }

    // 2. Verify Architecture: ARM64
    if (hdr.cputype != kMachOCpuTypeARM64) {
        out_result.status = UserspaceMachOLoadStatus::UnsupportedCpuType;
        out_result.error_message = "Non-ARM64 CPU type rejected (expected CPU_TYPE_ARM64)";
        return false;
    }

    // 3. Verify CPU Subtype: arm64 / arm64e
    const std::uint32_t masked_subtype = (hdr.cpusubtype & kMachOCpuSubtypeMask);
    if (masked_subtype != kMachOCpuSubtypeArm64All &&
        masked_subtype != kMachOCpuSubtypeArm64V8 &&
        masked_subtype != kMachOCpuSubtypeArm64E) {
        out_result.status = UserspaceMachOLoadStatus::UnsupportedCpuSubtype;
        out_result.error_message = "Unsupported ARM64 CPU subtype";
        return false;
    }

    out_result.is_arm64e = (masked_subtype == kMachOCpuSubtypeArm64E) ||
                           ((hdr.cpusubtype & kMachOCpuSubtypePtrAuth) != 0);

    // 4. Verify File Type: Execute, Dylib, Dylinker, Bundle
    if (hdr.filetype != kMachOFileTypeExecute &&
        hdr.filetype != kMachOFileTypeDylib &&
        hdr.filetype != kMachOFileTypeDylinker &&
        hdr.filetype != kMachOFileTypeBundle) {
        out_result.status = UserspaceMachOLoadStatus::UnsupportedFileType;
        out_result.error_message = "Unsupported Mach-O file type";
        return false;
    }

    out_result.is_pie = (hdr.flags & kMachOFlagPie) != 0;

    // 5. Verify Load Commands Size Bounds
    if (sizeof(MachOHeader64Raw) + hdr.sizeofcmds > size) {
        out_result.status = UserspaceMachOLoadStatus::CorruptedLoadCommands;
        out_result.error_message = "sizeofcmds exceeds file bounds";
        return false;
    }

    std::size_t cmd_offset = sizeof(MachOHeader64Raw);
    const std::size_t cmds_end = cmd_offset + hdr.sizeofcmds;
    bool has_lc_main = false;
    std::uint64_t main_entryoff = 0;
    std::uint64_t text_vmaddr = 0;
    bool has_text_seg = false;

    for (std::uint32_t i = 0; i < hdr.ncmds; ++i) {
        if (cmd_offset + sizeof(MachOLoadCommandRaw) > cmds_end) {
            out_result.status = UserspaceMachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Load command header exceeds sizeofcmds bounds";
            return false;
        }

        const auto* cmd = reinterpret_cast<const MachOLoadCommandRaw*>(bytes + cmd_offset);
        if (cmd->cmdsize < sizeof(MachOLoadCommandRaw) || (cmd->cmdsize % 4 != 0) ||
            (cmd_offset + cmd->cmdsize > cmds_end)) {
            out_result.status = UserspaceMachOLoadStatus::CorruptedLoadCommands;
            out_result.error_message = "Invalid or corrupted load command size";
            return false;
        }

        switch (cmd->cmd) {
            case kCmdSegment64: {
                if (cmd->cmdsize < sizeof(MachOSegment64Raw)) {
                    out_result.status = UserspaceMachOLoadStatus::CorruptedLoadCommands;
                    out_result.error_message = "LC_SEGMENT_64 command size truncated";
                    return false;
                }
                const auto* seg = reinterpret_cast<const MachOSegment64Raw*>(bytes + cmd_offset);
                UserspaceSegment s;
                s.name = sanitize_segname(seg->segname);
                s.vmaddr = seg->vmaddr;
                s.vmsize = seg->vmsize;
                s.fileoff = seg->fileoff;
                s.filesize = seg->filesize;
                s.maxprot = seg->maxprot;
                s.initprot = seg->initprot;
                s.is_pagezero = (s.name == "__PAGEZERO");

                if (s.name == "__TEXT") {
                    text_vmaddr = s.vmaddr;
                    has_text_seg = true;
                }

                out_result.segments.push_back(std::move(s));
                break;
            }
            case kCmdMain: {
                if (cmd->cmdsize < sizeof(MachOMainRaw)) {
                    out_result.status = UserspaceMachOLoadStatus::CorruptedLoadCommands;
                    out_result.error_message = "LC_MAIN command size truncated";
                    return false;
                }
                const auto* m = reinterpret_cast<const MachOMainRaw*>(bytes + cmd_offset);
                has_lc_main = true;
                main_entryoff = m->entryoff;
                break;
            }
            case kCmdLoadDylinker: {
                if (cmd->cmdsize > 12) {
                    std::uint32_t stroff = *reinterpret_cast<const std::uint32_t*>(bytes + cmd_offset + 8);
                    if (stroff < cmd->cmdsize && (cmd_offset + stroff < cmds_end)) {
                        const char* s = reinterpret_cast<const char*>(bytes + cmd_offset + stroff);
                        std::size_t maxlen = cmd->cmdsize - stroff;
                        std::size_t slen = 0;
                        while (slen < maxlen && s[slen] != '\0') slen++;
                        out_result.dylinker_path = std::string(s, slen);
                    }
                }
                break;
            }
            case kCmdLoadDylib:
            case kCmdLoadWeakDylib: {
                if (cmd->cmdsize > 24) {
                    std::uint32_t stroff = *reinterpret_cast<const std::uint32_t*>(bytes + cmd_offset + 8);
                    std::uint32_t cur_ver = *reinterpret_cast<const std::uint32_t*>(bytes + cmd_offset + 12);
                    std::uint32_t comp_ver = *reinterpret_cast<const std::uint32_t*>(bytes + cmd_offset + 16);
                    if (stroff < cmd->cmdsize && (cmd_offset + stroff < cmds_end)) {
                        const char* s = reinterpret_cast<const char*>(bytes + cmd_offset + stroff);
                        std::size_t maxlen = cmd->cmdsize - stroff;
                        std::size_t slen = 0;
                        while (slen < maxlen && s[slen] != '\0') slen++;
                        UserspaceDylibDep dep;
                        dep.path = std::string(s, slen);
                        dep.is_weak = (cmd->cmd == kCmdLoadWeakDylib);
                        dep.current_version = cur_ver;
                        dep.compatibility_version = comp_ver;
                        out_result.dylibs.push_back(std::move(dep));
                    }
                }
                break;
            }
            case kCmdBuildVersion: {
                if (cmd->cmdsize >= sizeof(MachOBuildVersionRaw)) {
                    const auto* bv = reinterpret_cast<const MachOBuildVersionRaw*>(bytes + cmd_offset);
                    out_result.min_os_version = format_version(bv->minos);
                    out_result.sdk_version = format_version(bv->sdk);
                }
                break;
            }
            case kCmdCodeSignature: {
                if (cmd->cmdsize >= sizeof(MachOLinkeditDataRaw)) {
                    const auto* cs = reinterpret_cast<const MachOLinkeditDataRaw*>(bytes + cmd_offset);
                    out_result.has_code_signature = true;
                    out_result.code_signature_offset = cs->dataoff;
                    out_result.code_signature_size = cs->datasize;
                }
                break;
            }
            default:
                break;
        }

        cmd_offset += cmd->cmdsize;
    }

    // Resolve Entry Point
    if (has_lc_main) {
        if (has_text_seg) {
            out_result.entry_point = text_vmaddr + main_entryoff;
        } else if (!out_result.segments.empty()) {
            out_result.entry_point = out_result.segments.front().vmaddr + main_entryoff;
        } else {
            out_result.entry_point = main_entryoff;
        }
    } else {
        // Fallback: first executable segment
        for (const auto& s : out_result.segments) {
            if (s.is_executable() && !s.is_pagezero) {
                out_result.entry_point = s.vmaddr;
                break;
            }
        }
    }

    if (out_result.entry_point == 0 && hdr.filetype == kMachOFileTypeExecute) {
        out_result.status = UserspaceMachOLoadStatus::MissingEntryPoint;
        out_result.error_message = "Executable Mach-O has no valid entry point";
        return false;
    }

    // Compute SHA-256
    out_result.sha256_hex = LinuxImageLoader::compute_sha256(data, size);

    out_result.status = UserspaceMachOLoadStatus::Success;
    return true;
}

UserspaceMachOLoadStatus UserspaceMachOLoader::validate_segments(
    const std::vector<UserspaceSegment>& segments,
    std::size_t file_size,
    std::uint64_t page_size,
    std::string& out_error) {

    bool has_executable = false;

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const auto& s = segments[i];

        // 1. Integer Overflow Checks
        if (s.vmsize > 0 && (s.vmaddr + s.vmsize < s.vmaddr)) {
            out_error = "Segment '" + s.name + "' virtual memory range overflows 64-bit address space";
            return UserspaceMachOLoadStatus::IntegerOverflow;
        }
        if (s.filesize > 0 && (s.fileoff + s.filesize < s.fileoff)) {
            out_error = "Segment '" + s.name + "' file offset range overflows";
            return UserspaceMachOLoadStatus::IntegerOverflow;
        }

        // 2. File Bounds Check
        if (s.filesize > 0 && (s.fileoff + s.filesize > file_size)) {
            out_error = "Segment '" + s.name + "' payload exceeds file boundary";
            return UserspaceMachOLoadStatus::SegmentOutOfBounds;
        }

        // 3. Page Alignment Checks
        if (page_size > 0) {
            if ((s.vmaddr % page_size) != 0) {
                out_error = "Segment '" + s.name + "' vmaddr is not page-aligned (" +
                            std::to_string(page_size) + " bytes)";
                return UserspaceMachOLoadStatus::MisalignedSegment;
            }
            if ((s.vmsize % page_size) != 0) {
                out_error = "Segment '" + s.name + "' vmsize is not page-aligned (" +
                            std::to_string(page_size) + " bytes)";
                return UserspaceMachOLoadStatus::MisalignedSegment;
            }
            if (s.filesize > 0 && (s.fileoff % page_size) != 0) {
                out_error = "Segment '" + s.name + "' fileoff is not page-aligned (" +
                            std::to_string(page_size) + " bytes)";
                return UserspaceMachOLoadStatus::MisalignedSegment;
            }
        }

        // 4. Overlap Check with previous segments
        for (std::size_t j = 0; j < i; ++j) {
            const auto& prev = segments[j];
            if (s.vmsize == 0 || prev.vmsize == 0) continue;

            const std::uint64_t start_a = s.vmaddr;
            const std::uint64_t end_a   = s.vmaddr + s.vmsize;
            const std::uint64_t start_b = prev.vmaddr;
            const std::uint64_t end_b   = prev.vmaddr + prev.vmsize;

            if (start_a < end_b && start_b < end_a) {
                out_error = "Segment '" + s.name + "' overlaps with segment '" + prev.name + "'";
                return UserspaceMachOLoadStatus::OverlappingSegments;
            }
        }

        if (!s.is_pagezero && s.is_executable()) {
            has_executable = true;
        }
    }

    if (!has_executable && !segments.empty()) {
        out_error = "Mach-O contains no executable segment";
        return UserspaceMachOLoadStatus::NoExecutableSegment;
    }

    return UserspaceMachOLoadStatus::Success;
}

namespace {

// Helper to translate userspace virtual address to backing Memory buffer offset
inline std::size_t translate_userspace_vaddr(
    std::uint64_t vaddr,
    std::size_t mem_size,
    std::uint64_t stack_top,
    std::uint64_t base_vaddr) noexcept {

    if (vaddr < mem_size) {
        return static_cast<std::size_t>(vaddr);
    }
    // High-address stack region (canonical 48-bit userspace)
    if (vaddr >= 0x0000700000000000ULL && stack_top <= 0x00007FFFFFFFFFFFULL && vaddr <= stack_top) {
        std::uint64_t dist = stack_top - vaddr;
        if (dist < mem_size) {
            return mem_size - static_cast<std::size_t>(dist);
        }
    }
    // Normal 4GB binary region
    if (base_vaddr > 0 && vaddr >= base_vaddr) {
        std::uint64_t rel = vaddr - base_vaddr;
        if (rel < mem_size) {
            return static_cast<std::size_t>(rel);
        }
    }
    // Out of bounds address returns mem_size
    return mem_size;
}

} // anonymous namespace

UserspaceLoadResult UserspaceMachOLoader::load_into_memory(
    const void* data,
    std::size_t size,
    Memory& memory,
    const UserspaceLoadConfig& config) {

    UserspaceLoadResult result;
    if (!parse_metadata(data, size, result)) {
        return result;
    }

    std::string val_err;
    auto val_status = validate_segments(result.segments, size, config.page_size, val_err);
    if (val_status != UserspaceMachOLoadStatus::Success) {
        result.status = val_status;
        result.error_message = val_err;
        return result;
    }

    const std::uint64_t slide = config.aslr_enabled ? config.aslr_slide : 0ULL;
    result.aslr_slide = slide;
    result.entry_point += slide;

    const auto* file_bytes = static_cast<const std::uint8_t*>(data);

    // Identify lowest non-pagezero segment base address
    std::uint64_t lowest_load_vaddr = 0;
    for (const auto& s : result.segments) {
        if (!s.is_pagezero && s.vmsize > 0) {
            if (lowest_load_vaddr == 0 || s.vmaddr < lowest_load_vaddr) {
                lowest_load_vaddr = s.vmaddr;
            }
        }
    }

    // Map segments
    for (auto& s : result.segments) {
        s.mapped_vaddr = s.vmaddr + (s.is_pagezero ? 0ULL : slide);

        if (s.is_pagezero || s.filesize == 0) {
            continue; // PAGEZERO and zero-fill pages need no physical file writes
        }

        for (std::uint64_t b = 0; b < s.filesize; ++b) {
            std::uint64_t target_vaddr = s.mapped_vaddr + b;
            std::size_t mem_off = translate_userspace_vaddr(
                target_vaddr, memory.size(), config.stack_top, lowest_load_vaddr + slide);

            if (mem_off >= memory.size()) {
                result.status = UserspaceMachOLoadStatus::MemoryWriteFailed;
                result.error_message = "Segment write out of memory bounds for segment " + s.name;
                return result;
            }

            memory.write_u8(mem_off, file_bytes[s.fileoff + b]);
        }
    }

    // Construct userspace stack
    std::string stack_err;
    if (!construct_initial_stack(
            memory,
            config.stack_top,
            config.argv,
            config.envp,
            config.executable_path,
            result.initial_sp,
            stack_err)) {
        result.status = UserspaceMachOLoadStatus::MemoryWriteFailed;
        result.error_message = "Failed to construct userspace stack: " + stack_err;
        return result;
    }

    result.status = UserspaceMachOLoadStatus::Success;
    return result;
}

bool UserspaceMachOLoader::construct_initial_stack(
    Memory& memory,
    std::uint64_t stack_top,
    const std::vector<std::string>& argv,
    const std::vector<std::string>& envp,
    const std::string& exec_path,
    std::uint64_t& out_sp,
    std::string& out_error) {

    if (stack_top == 0 || memory.size() == 0) {
        out_error = "Stack top or memory size is zero";
        return false;
    }
    if (stack_top >= 0x8000000000000000ULL) {
        out_error = "Stack write out of bounds: stack top in kernel address space";
        return false;
    }

    // Ensure 16-byte aligned stack top
    std::uint64_t curr_vaddr = stack_top & ~0xFULL;

    // Helper to safely write a byte to memory
    auto write_byte = [&](std::uint64_t vaddr, std::uint8_t val) -> bool {
        std::size_t off = translate_userspace_vaddr(vaddr, memory.size(), stack_top, 0);
        if (off >= memory.size()) {
            out_error = "Stack write out of bounds";
            return false;
        }
        memory.write_u8(off, val);
        return true;
    };

    // Helper to write a 64-bit integer
    auto write_u64 = [&](std::uint64_t vaddr, std::uint64_t val) -> bool {
        std::size_t off = translate_userspace_vaddr(vaddr, memory.size(), stack_top, 0);
        if (off + 8 > memory.size()) {
            out_error = "Stack u64 write out of bounds";
            return false;
        }
        memory.write_u64(off, val);
        return true;
    };

    // Push strings onto stack from high to low address
    // 1. Executable path (apple[0])
    std::vector<std::uint64_t> apple_ptrs;
    std::string effective_exec_path = exec_path.empty() ? (argv.empty() ? "/sbin/launchd" : argv[0]) : exec_path;
    {
        std::size_t len = effective_exec_path.size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < effective_exec_path.size()) ? static_cast<std::uint8_t>(effective_exec_path[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        apple_ptrs.push_back(curr_vaddr);
    }

    // 2. envp strings
    std::vector<std::uint64_t> envp_ptrs;
    for (auto it = envp.rbegin(); it != envp.rend(); ++it) {
        std::size_t len = it->size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < it->size()) ? static_cast<std::uint8_t>((*it)[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        envp_ptrs.push_back(curr_vaddr);
    }
    std::reverse(envp_ptrs.begin(), envp_ptrs.end());

    // 3. argv strings
    std::vector<std::uint64_t> argv_ptrs;
    for (auto it = argv.rbegin(); it != argv.rend(); ++it) {
        std::size_t len = it->size() + 1;
        curr_vaddr -= len;
        for (std::size_t i = 0; i < len; ++i) {
            std::uint8_t ch = (i < it->size()) ? static_cast<std::uint8_t>((*it)[i]) : 0;
            if (!write_byte(curr_vaddr + i, ch)) return false;
        }
        argv_ptrs.push_back(curr_vaddr);
    }
    std::reverse(argv_ptrs.begin(), argv_ptrs.end());

    // 4. Align down to 8 bytes for pointer arrays
    curr_vaddr &= ~0x7ULL;

    // Calculate total pointer slots:
    // argc (1) + argv_ptrs (N) + NULL (1) + envp_ptrs (M) + NULL (1) + apple_ptrs (K) + NULL (1)
    std::size_t total_slots = 1 + argv_ptrs.size() + 1 + envp_ptrs.size() + 1 + apple_ptrs.size() + 1;
    curr_vaddr -= (total_slots * sizeof(std::uint64_t));

    // Align stack pointer to 16 bytes (ARM64 ABI requirement)
    curr_vaddr &= ~0xFULL;

    std::uint64_t sp_walker = curr_vaddr;

    // Write argc
    if (!write_u64(sp_walker, argv_ptrs.size())) return false;
    sp_walker += 8;

    // Write argv pointers
    for (std::uint64_t ptr : argv_ptrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator
    sp_walker += 8;

    // Write envp pointers
    for (std::uint64_t ptr : envp_ptrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator
    sp_walker += 8;

    // Write apple pointers
    for (std::uint64_t ptr : apple_ptrs) {
        if (!write_u64(sp_walker, ptr)) return false;
        sp_walker += 8;
    }
    if (!write_u64(sp_walker, 0)) return false; // NULL terminator

    out_sp = curr_vaddr;
    return true;
}

std::vector<std::uint8_t> UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
    const std::string& dylinker,
    const std::vector<std::string>& dylibs,
    const std::vector<std::uint8_t>& custom_code) {

    std::vector<std::uint8_t> buf;

    // Total file size: 0xC000 bytes (3 pages of 16KB: TEXT at 0, DATA at 0x4000, LINKEDIT at 0x8000)
    constexpr std::size_t kTotalFileSize = 0xC000u;
    buf.assign(kTotalFileSize, 0);

    // Setup Header
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(buf.data());
    hdr->magic = kMachO64Magic;
    hdr->cputype = kMachOCpuTypeARM64;
    hdr->cpusubtype = kMachOCpuSubtypeArm64E | kMachOCpuSubtypePtrAuth;
    hdr->filetype = kMachOFileTypeExecute;
    hdr->flags = kMachOFlagPie | kMachOFlagTwoLevel | kMachOFlagDyldLink;
    hdr->reserved = 0;

    std::size_t cmd_off = sizeof(MachOHeader64Raw);
    std::uint32_t ncmds = 0;

    auto append_cmd = [&](const void* cmd_data, std::size_t cmd_sz) {
        std::memcpy(buf.data() + cmd_off, cmd_data, cmd_sz);
        cmd_off += cmd_sz;
        ncmds++;
    };

    // 1. __PAGEZERO
    MachOSegment64Raw seg_pagezero{};
    seg_pagezero.cmd = kCmdSegment64;
    seg_pagezero.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(seg_pagezero.segname, "__PAGEZERO", 10);
    seg_pagezero.vmaddr = 0x0ULL;
    seg_pagezero.vmsize = 0x100000000ULL; // 4GB
    seg_pagezero.fileoff = 0;
    seg_pagezero.filesize = 0;
    seg_pagezero.maxprot = kProtNone;
    seg_pagezero.initprot = kProtNone;
    append_cmd(&seg_pagezero, sizeof(seg_pagezero));

    // 2. __TEXT
    MachOSegment64Raw seg_text{};
    seg_text.cmd = kCmdSegment64;
    seg_text.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(seg_text.segname, "__TEXT", 6);
    seg_text.vmaddr = 0x100000000ULL;
    seg_text.vmsize = kApplePageSize16K;
    seg_text.fileoff = 0;
    seg_text.filesize = kApplePageSize16K;
    seg_text.maxprot = kProtRead | kProtExecute;
    seg_text.initprot = kProtRead | kProtExecute;
    append_cmd(&seg_text, sizeof(seg_text));

    // 3. __DATA
    MachOSegment64Raw seg_data{};
    seg_data.cmd = kCmdSegment64;
    seg_data.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(seg_data.segname, "__DATA", 6);
    seg_data.vmaddr = 0x100004000ULL;
    seg_data.vmsize = kApplePageSize16K;
    seg_data.fileoff = 0x4000ULL;
    seg_data.filesize = kApplePageSize16K;
    seg_data.maxprot = kProtRead | kProtWrite;
    seg_data.initprot = kProtRead | kProtWrite;
    append_cmd(&seg_data, sizeof(seg_data));

    // 4. __LINKEDIT
    MachOSegment64Raw seg_linkedit{};
    seg_linkedit.cmd = kCmdSegment64;
    seg_linkedit.cmdsize = sizeof(MachOSegment64Raw);
    std::memcpy(seg_linkedit.segname, "__LINKEDIT", 10);
    seg_linkedit.vmaddr = 0x100008000ULL;
    seg_linkedit.vmsize = kApplePageSize16K;
    seg_linkedit.fileoff = 0x8000ULL;
    seg_linkedit.filesize = kApplePageSize16K;
    seg_linkedit.maxprot = kProtRead;
    seg_linkedit.initprot = kProtRead;
    append_cmd(&seg_linkedit, sizeof(seg_linkedit));

    // 5. LC_MAIN
    MachOMainRaw cmd_main{};
    cmd_main.cmd = kCmdMain;
    cmd_main.cmdsize = sizeof(MachOMainRaw);
    cmd_main.entryoff = 0x1000ULL; // Within __TEXT
    cmd_main.stacksize = 0;
    append_cmd(&cmd_main, sizeof(cmd_main));

    // 6. LC_LOAD_DYLINKER
    if (!dylinker.empty()) {
        std::vector<std::uint8_t> dylinker_cmd;
        std::size_t raw_sz = 12 + dylinker.size() + 1;
        std::size_t aligned_sz = (raw_sz + 7) & ~7ULL;
        dylinker_cmd.assign(aligned_sz, 0);

        auto* pcmd = reinterpret_cast<std::uint32_t*>(dylinker_cmd.data());
        pcmd[0] = kCmdLoadDylinker;
        pcmd[1] = static_cast<std::uint32_t>(aligned_sz);
        pcmd[2] = 12; // offset to string
        std::memcpy(dylinker_cmd.data() + 12, dylinker.c_str(), dylinker.size() + 1);

        append_cmd(dylinker_cmd.data(), dylinker_cmd.size());
    }

    // 7. LC_LOAD_DYLIB
    for (const auto& dylib_path : dylibs) {
        std::vector<std::uint8_t> dylib_cmd;
        std::size_t raw_sz = 24 + dylib_path.size() + 1;
        std::size_t aligned_sz = (raw_sz + 7) & ~7ULL;
        dylib_cmd.assign(aligned_sz, 0);

        auto* pcmd = reinterpret_cast<std::uint32_t*>(dylib_cmd.data());
        pcmd[0] = kCmdLoadDylib;
        pcmd[1] = static_cast<std::uint32_t>(aligned_sz);
        pcmd[2] = 24; // offset to string
        pcmd[3] = 0x00010000; // current version 1.0.0
        pcmd[4] = 0x00010000; // comp version 1.0.0

        std::memcpy(dylib_cmd.data() + 24, dylib_path.c_str(), dylib_path.size() + 1);
        append_cmd(dylib_cmd.data(), dylib_cmd.size());
    }

    // 8. LC_BUILD_VERSION (iOS 17.0, SDK 17.0)
    MachOBuildVersionRaw bv{};
    bv.cmd = kCmdBuildVersion;
    bv.cmdsize = sizeof(MachOBuildVersionRaw);
    bv.platform = 2; // PLATFORM_IOS
    bv.minos = (17 << 16) | (0 << 8);
    bv.sdk = (17 << 16) | (0 << 8);
    bv.ntools = 0;
    append_cmd(&bv, sizeof(bv));

    // 9. LC_CODE_SIGNATURE
    MachOLinkeditDataRaw cs{};
    cs.cmd = kCmdCodeSignature;
    cs.cmdsize = sizeof(MachOLinkeditDataRaw);
    cs.dataoff = 0x8100;
    cs.datasize = 0x200;
    append_cmd(&cs, sizeof(cs));

    // Update Header
    hdr->ncmds = ncmds;
    hdr->sizeofcmds = static_cast<std::uint32_t>(cmd_off - sizeof(MachOHeader64Raw));

    // Write real ARM64 instructions at entryoff (0x1000)
    if (!custom_code.empty()) {
        std::memcpy(buf.data() + 0x1000, custom_code.data(),
                    std::min(custom_code.size(), static_cast<std::size_t>(kApplePageSize16K - 0x1000)));
    } else {
        std::uint32_t code[] = { 0xD2800000u, 0xD65F03C0u };
        std::memcpy(buf.data() + 0x1000, code, sizeof(code));
    }

    return buf;
}

std::vector<std::uint8_t> UserspaceMachOFixtureBuilder::create_valid_arm64_executable() {
    auto buf = create_valid_arm64e_executable("/usr/lib/dyld", {"/usr/lib/libSystem.B.dylib"});
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(buf.data());
    hdr->cpusubtype = kMachOCpuSubtypeArm64All;
    return buf;
}

} // namespace winios::loader
