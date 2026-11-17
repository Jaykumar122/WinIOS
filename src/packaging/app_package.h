#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::packaging {

struct FileEntry {
    std::string path; // Relative path inside bundle, e.g. "bin/calc", "res/icon.png"
    std::uint64_t size{0};
    std::string sha256_hex;
    std::vector<std::uint8_t> data;
};

struct PackageManifest {
    std::string bundle_id;
    std::string name;
    std::string version{"1.0.0"};
    std::string min_os_version{"215.0"};
    std::string executable_name;
    std::vector<std::string> permissions;
    std::vector<FileEntry> files;

    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] std::string serialize() const;
    static bool deserialize(const std::string& text, PackageManifest& out_manifest);
};

struct AppPackage {
    PackageManifest manifest;

    // Helper to add a file to package
    void add_file(const std::string& rel_path, std::vector<std::uint8_t> data);

    // Serialization to single package payload (.wios container)
    [[nodiscard]] std::vector<std::uint8_t> pack() const;

    // Deserialization from package payload
    static bool unpack(const std::uint8_t* data, std::size_t size, AppPackage& out_pkg);
};

} // namespace winios::packaging
