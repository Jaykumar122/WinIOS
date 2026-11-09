#include "loader/rootfs_builder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace winios::loader {

namespace {

inline void format_hex8(char* dest, std::uint32_t val) {
    static constexpr const char* kHex = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) {
        dest[i] = kHex[val & 0xFu];
        val >>= 4u;
    }
}

inline std::uint32_t parse_hex8(const char* src) {
    char tmp[9] = {0};
    std::memcpy(tmp, src, 8);
    return static_cast<std::uint32_t>(std::strtoul(tmp, nullptr, 16));
}

void append_cpio_entry(std::vector<std::uint8_t>& buf,
                       std::uint32_t ino,
                       std::uint32_t mode,
                       std::uint32_t uid,
                       std::uint32_t gid,
                       const std::string& name,
                       const void* data,
                       std::size_t data_size) {
    char header[112] = {0};
    std::memcpy(header, "070701", 6);
    format_hex8(header + 6, ino);
    format_hex8(header + 14, mode);
    format_hex8(header + 22, uid);
    format_hex8(header + 30, gid);
    format_hex8(header + 38, 1); // nlink
    format_hex8(header + 46, 0); // mtime
    format_hex8(header + 54, static_cast<std::uint32_t>(data_size));
    format_hex8(header + 62, 0); // devmajor
    format_hex8(header + 70, 0); // devminor
    format_hex8(header + 78, 0); // rdevmajor
    format_hex8(header + 86, 0); // rdevminor

    std::uint32_t namesize = static_cast<std::uint32_t>(name.size() + 1);
    format_hex8(header + 94, namesize);
    format_hex8(header + 102, 0); // check

    // Append header
    buf.insert(buf.end(), header, header + 110);

    // Append filename (null-terminated)
    buf.insert(buf.end(), name.begin(), name.end());
    buf.push_back(0);

    // Pad to 4-byte boundary
    while ((buf.size() % 4) != 0) {
        buf.push_back(0);
    }

    // Append data
    if (data && data_size > 0) {
        const auto* byte_data = static_cast<const std::uint8_t*>(data);
        buf.insert(buf.end(), byte_data, byte_data + data_size);
        while ((buf.size() % 4) != 0) {
            buf.push_back(0);
        }
    }
}

} // anonymous namespace

void RootfsBuilder::add_file(const std::string& path, const std::vector<std::uint8_t>& data, std::uint32_t mode) {
    RootfsEntry ent;
    ent.path = path;
    ent.mode = mode;
    ent.uid = 0;
    ent.gid = 0;
    ent.data = data;
    entries_.push_back(std::move(ent));
}

void RootfsBuilder::add_file_text(const std::string& path, const std::string& text, std::uint32_t mode) {
    std::vector<std::uint8_t> data(text.begin(), text.end());
    add_file(path, data, mode);
}

void RootfsBuilder::add_directory(const std::string& path, std::uint32_t mode) {
    RootfsEntry ent;
    ent.path = path;
    ent.mode = mode;
    ent.uid = 0;
    ent.gid = 0;
    ent.data.clear();
    entries_.push_back(std::move(ent));
}

std::vector<std::uint8_t> RootfsBuilder::build_cpio() const {
    std::vector<std::uint8_t> buf;
    buf.reserve(4096);

    std::uint32_t ino = 1;
    for (const auto& ent : entries_) {
        // Normalize path: strip leading slash for CPIO convention
        std::string norm_path = ent.path;
        if (!norm_path.empty() && norm_path[0] == '/') {
            norm_path = norm_path.substr(1);
        }
        if (norm_path.empty()) norm_path = ".";

        append_cpio_entry(buf, ino++, ent.mode, ent.uid, ent.gid, norm_path,
                          ent.data.data(), ent.data.size());
    }

    // Append TRAILER!!!
    append_cpio_entry(buf, 0, 0, 0, 0, "TRAILER!!!", nullptr, 0);

    // Pad total archive to 512 bytes boundary
    while ((buf.size() % 512) != 0) {
        buf.push_back(0);
    }

    return buf;
}

std::vector<std::uint8_t> RootfsBuilder::create_minimal_synthetic_rootfs(
    const std::vector<std::uint8_t>& init_binary_bytes) {

    RootfsBuilder builder;

    // Essential system directories
    builder.add_directory("bin", 0040755);
    builder.add_directory("sbin", 0040755);
    builder.add_directory("dev", 0040755);
    builder.add_directory("proc", 0040755);
    builder.add_directory("sys", 0040755);
    builder.add_directory("etc", 0040755);
    builder.add_directory("root", 0040700);

    // Basic system files
    builder.add_file_text("etc/passwd", "root:x:0:0:root:/root:/bin/sh\n", 0100644);
    builder.add_file_text("etc/group", "root:x:0:\n", 0100644);
    builder.add_file_text("etc/hostname", "win-ios-vm\n", 0100644);

    // /init executable or fallback script
    if (!init_binary_bytes.empty()) {
        builder.add_file("init", init_binary_bytes, 0100755);
    } else {
        std::string fallback_script =
            "#!/bin/sh\n"
            "echo \"[init] WinIOS Minimal RootFS Initialized\"\n"
            "exit 0\n";
        builder.add_file_text("init", fallback_script, 0100755);
    }

    return builder.build_cpio();
}

std::vector<RootfsEntry> RootfsBuilder::parse_cpio(const void* data, std::size_t size, std::string* out_error) {
    std::vector<RootfsEntry> result;
    if (!data || size < 110) {
        if (out_error) *out_error = "Archive truncated or empty";
        return result;
    }

    const auto* p = static_cast<const char*>(data);
    std::size_t offset = 0;

    while (offset + 110 <= size) {
        if (std::memcmp(p + offset, "070701", 6) != 0 && std::memcmp(p + offset, "070702", 6) != 0) {
            if (out_error) *out_error = "Invalid CPIO magic at offset " + std::to_string(offset);
            break;
        }

        const char* hdr = p + offset;
        std::uint32_t mode = parse_hex8(hdr + 14);
        std::uint32_t uid  = parse_hex8(hdr + 22);
        std::uint32_t gid  = parse_hex8(hdr + 30);
        std::uint32_t filesize = parse_hex8(hdr + 54);
        std::uint32_t namesize = parse_hex8(hdr + 94);

        offset += 110;
        if (offset + namesize > size) {
            if (out_error) *out_error = "Truncated filename in CPIO archive";
            break;
        }

        std::string name(p + offset, (namesize > 0) ? (namesize - 1) : 0);
        offset += namesize;

        // Align name to 4 bytes
        while ((offset % 4) != 0 && offset < size) {
            offset++;
        }

        if (name == "TRAILER!!!") {
            break;
        }

        if (offset + filesize > size) {
            if (out_error) *out_error = "Truncated file data in CPIO archive";
            break;
        }

        RootfsEntry entry;
        entry.path = name;
        entry.mode = mode;
        entry.uid  = uid;
        entry.gid  = gid;

        if (filesize > 0) {
            const auto* file_bytes = reinterpret_cast<const std::uint8_t*>(p + offset);
            entry.data.assign(file_bytes, file_bytes + filesize);
        }

        result.push_back(std::move(entry));
        offset += filesize;

        // Align data to 4 bytes
        while ((offset % 4) != 0 && offset < size) {
            offset++;
        }
    }

    return result;
}

bool RootfsBuilder::has_file(const void* data, std::size_t size, const std::string& path) {
    std::string norm = path;
    if (!norm.empty() && norm[0] == '/') {
        norm = norm.substr(1);
    }
    auto entries = parse_cpio(data, size);
    for (const auto& e : entries) {
        if (e.path == norm) return true;
    }
    return false;
}

bool RootfsBuilder::get_file_content(const void* data, std::size_t size, const std::string& path, std::vector<std::uint8_t>& out_content) {
    std::string norm = path;
    if (!norm.empty() && norm[0] == '/') {
        norm = norm.substr(1);
    }
    auto entries = parse_cpio(data, size);
    for (const auto& e : entries) {
        if (e.path == norm) {
            out_content = e.data;
            return true;
        }
    }
    return false;
}

} // namespace winios::loader
