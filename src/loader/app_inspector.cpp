#include "loader/app_inspector.h"

#include <cstring>
#include <fstream>

namespace winios::loader {

AppInspectionReport AppInspector::inspect_binary(const uint8_t* data, size_t size) {
    AppInspectionReport report{};
    report.binary_size_bytes = size;

    if (!data || size < kMinMachOSize) {
        report.is_valid = false;
        report.rejection_reason = "Binary is null or below minimum Mach-O header size (32 bytes)";
        return report;
    }

    if (size > kMaxMachOSize) {
        report.is_valid = false;
        report.rejection_reason = "Binary exceeds maximum size threshold (64 MB)";
        return report;
    }

    // Read Mach-O 64-bit Header
    std::memcpy(&report.magic, data, 4);
    std::memcpy(&report.cputype, data + 4, 4);
    std::memcpy(&report.cpusubtype, data + 8, 4);
    std::memcpy(&report.filetype, data + 12, 4);
    std::memcpy(&report.ncmds, data + 16, 4);
    std::memcpy(&report.sizeofcmds, data + 20, 4);

    if (report.magic != kMachO64Magic) {
        report.is_valid = false;
        report.rejection_reason = "Invalid Mach-O 64-bit magic (expected 0xfeedfacf)";
        return report;
    }

    if (report.cputype != kCpuTypeArm64) {
        report.is_valid = false;
        report.rejection_reason = "Unsupported CPU architecture (expected ARM64 0x0100000c)";
        return report;
    }

    if (report.filetype != kMhExecute) {
        report.is_valid = false;
        report.rejection_reason = "Unsupported Mach-O file type (expected MH_EXECUTE = 2)";
        return report;
    }

    if (32 + report.sizeofcmds > size) {
        report.is_valid = false;
        report.rejection_reason = "Load command block extends beyond EOF";
        return report;
    }

    // Parse Load Commands
    size_t offset = 32;
    bool found_entry = false;
    report.wx_clean = true;

    for (uint32_t i = 0; i < report.ncmds; ++i) {
        if (offset + 8 > size) {
            report.is_valid = false;
            report.rejection_reason = "Truncated load command header";
            return report;
        }

        uint32_t cmd = 0;
        uint32_t cmdsize = 0;
        std::memcpy(&cmd, data + offset, 4);
        std::memcpy(&cmdsize, data + offset + 4, 4);

        if (cmdsize < 8 || offset + cmdsize > size) {
            report.is_valid = false;
            report.rejection_reason = "Malformed load command size";
            return report;
        }

        if (cmd == 0x19) { // LC_SEGMENT_64
            report.load_commands.push_back("LC_SEGMENT_64");
            if (cmdsize >= 72) {
                SegmentInfo seg{};
                char segname_buf[17] = {0};
                std::memcpy(segname_buf, data + offset + 8, 16);
                seg.segname = segname_buf;
                std::memcpy(&seg.vmaddr, data + offset + 24, 8);
                std::memcpy(&seg.vmsize, data + offset + 32, 8);
                std::memcpy(&seg.fileoff, data + offset + 40, 8);
                std::memcpy(&seg.filesize, data + offset + 48, 8);
                std::memcpy(&seg.maxprot, data + offset + 56, 4);
                std::memcpy(&seg.initprot, data + offset + 60, 4);
                std::memcpy(&seg.nsects, data + offset + 64, 4);

                // Segment file range bounds check
                if (seg.filesize > 0 && (seg.fileoff + seg.filesize > size)) {
                    report.is_valid = false;
                    report.rejection_reason = "Segment file offset + filesize exceeds binary bounds";
                    return report;
                }

                // W^X enforcement check (initprot must not be both writable (2) and executable (4))
                if ((seg.initprot & 0x2) && (seg.initprot & 0x4)) {
                    report.wx_clean = false;
                }

                // Check 16 KB page granule alignment (0x4000) for segments
                if (seg.vmaddr != 0 && (seg.vmaddr & 0x3FFFull) != 0) {
                    report.page_aligned_16k = false;
                }

                report.segments.push_back(seg);
            }
        } else if (cmd == 0x80000028) { // LC_MAIN
            report.load_commands.push_back("LC_MAIN");
            if (cmdsize >= 24) {
                std::memcpy(&report.entry_point_offset, data + offset + 8, 8);
                found_entry = true;
            }
        } else if (cmd == 0x5) { // LC_UNIXTHREAD
            report.load_commands.push_back("LC_UNIXTHREAD");
            found_entry = true;
        } else if (cmd == 0x1d) { // LC_CODE_SIGNATURE
            report.load_commands.push_back("LC_CODE_SIGNATURE");
            report.has_code_signature = true;
            if (cmdsize >= 16) {
                std::memcpy(&report.code_signature_offset, data + offset + 8, 4);
                std::memcpy(&report.code_signature_size, data + offset + 12, 4);
            }
        } else if (cmd == 0xe) {
            report.load_commands.push_back("LC_LOAD_DYLINKER");
        } else if (cmd == 0xc) {
            report.load_commands.push_back("LC_LOAD_DYLIB");
        } else if (cmd == 0x32) {
            report.load_commands.push_back("LC_BUILD_VERSION");
        } else {
            report.load_commands.push_back("LC_OTHER_0x" + std::to_string(cmd));
        }

        offset += cmdsize;
    }

    if (!found_entry) {
        report.is_valid = false;
        report.rejection_reason = "Binary lacks an entry point load command (neither LC_MAIN nor LC_UNIXTHREAD)";
        return report;
    }

    if (!report.wx_clean) {
        report.is_valid = false;
        report.rejection_reason = "W^X violation: Segment has simultaneous write and execute permissions";
        return report;
    }

    report.is_valid = true;
    report.rejection_reason = "Valid ARM64 Mach-O Executable";
    return report;
}

AppInspectionReport AppInspector::inspect_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        AppInspectionReport report{};
        report.is_valid = false;
        report.rejection_reason = "Failed to open file: " + path;
        return report;
    }

    const auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(sz);
    if (!f.read(reinterpret_cast<char*>(buffer.data()), sz)) {
        AppInspectionReport report{};
        report.is_valid = false;
        report.rejection_reason = "Failed to read binary data from file: " + path;
        return report;
    }

    return inspect_binary(buffer.data(), buffer.size());
}

} // namespace winios::loader
