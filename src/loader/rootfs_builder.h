#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace winios::loader {

// -----------------------------------------------------------------------------
// RootfsEntry: Single file or directory entry in a root filesystem
// -----------------------------------------------------------------------------
struct RootfsEntry {
    std::string path;
    std::uint32_t mode{0100755}; // Default regular executable file
    std::uint32_t uid{0};
    std::uint32_t gid{0};
    std::vector<std::uint8_t> data;
};

// -----------------------------------------------------------------------------
// RootfsBuilder: In-memory CPIO (newc) archive generator and parser
// -----------------------------------------------------------------------------
class RootfsBuilder {
public:
    RootfsBuilder() = default;
    ~RootfsBuilder() = default;

    // Mutation
    void add_file(const std::string& path, const std::vector<std::uint8_t>& data, std::uint32_t mode = 0100755);
    void add_file_text(const std::string& path, const std::string& text, std::uint32_t mode = 0100644);
    void add_directory(const std::string& path, std::uint32_t mode = 0040755);
    void clear() noexcept { entries_.clear(); }

    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::vector<RootfsEntry>& entries() const noexcept { return entries_; }

    // Serialization to CPIO 'newc' format
    [[nodiscard]] std::vector<std::uint8_t> build_cpio() const;

    // Synthetic Standard Linux Root Filesystem (with /init, /bin, /dev, /proc, /sys, /etc)
    [[nodiscard]] static std::vector<std::uint8_t> create_minimal_synthetic_rootfs(const std::vector<std::uint8_t>& init_binary_bytes = {});

    // Parsing and Verification
    [[nodiscard]] static std::vector<RootfsEntry> parse_cpio(const void* data, std::size_t size, std::string* out_error = nullptr);
    [[nodiscard]] static bool has_file(const void* data, std::size_t size, const std::string& path);
    [[nodiscard]] static bool get_file_content(const void* data, std::size_t size, const std::string& path, std::vector<std::uint8_t>& out_content);

private:
    std::vector<RootfsEntry> entries_;
};

} // namespace winios::loader
