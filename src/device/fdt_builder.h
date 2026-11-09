#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "device/virtual_platform.h"

namespace winios::device {

// Standard FDT Magic: 0xd00dfeed (big-endian)
constexpr std::uint32_t kFdtMagic = 0xd00dfeed;
constexpr std::uint32_t kFdtVersion = 17;
constexpr std::uint32_t kFdtLastCompVersion = 16;

// FDT Tokens (big-endian in binary)
constexpr std::uint32_t kFdtBeginNode = 0x00000001;
constexpr std::uint32_t kFdtEndNode   = 0x00000002;
constexpr std::uint32_t kFdtProp      = 0x00000003;
constexpr std::uint32_t kFdtNop       = 0x00000004;
constexpr std::uint32_t kFdtEnd       = 0x00000009;

#pragma pack(push, 1)
struct FdtRawHeader {
    std::uint32_t magic;
    std::uint32_t totalsize;
    std::uint32_t off_dt_struct;
    std::uint32_t off_dt_strings;
    std::uint32_t off_mem_rsvmap;
    std::uint32_t version;
    std::uint32_t last_comp_version;
    std::uint32_t boot_cpuid_phys;
    std::uint32_t size_dt_strings;
    std::uint32_t size_dt_struct;
};

struct FdtMemReserveEntry {
    std::uint64_t address;
    std::uint64_t size;
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// FdtNode: Internal representation for tree construction
// -----------------------------------------------------------------------------
struct FdtProperty {
    std::string name;
    std::vector<std::uint8_t> data;
};

struct FdtNode {
    std::string name;
    std::vector<FdtProperty> properties;
    std::vector<std::shared_ptr<FdtNode>> children;

    FdtNode* find_child(std::string_view child_name) const;
    const FdtProperty* find_property(std::string_view prop_name) const;
};

// -----------------------------------------------------------------------------
// FdtBuilder: Constructs standard compliant Flattened Device Tree blobs
// -----------------------------------------------------------------------------
class FdtBuilder {
public:
    FdtBuilder();
    ~FdtBuilder() = default;

    // Direct node hierarchy building
    void begin_node(const std::string& name);
    void end_node();

    // Property helpers
    void add_property_empty(const std::string& name);
    void add_property_u32(const std::string& name, std::uint32_t value);
    void add_property_u64(const std::string& name, std::uint64_t value);
    void add_property_u32_array(const std::string& name, const std::vector<std::uint32_t>& values);
    void add_property_u64_array(const std::string& name, const std::vector<std::uint64_t>& values);
    void add_property_string(const std::string& name, const std::string& value);
    void add_property_string_list(const std::string& name, const std::vector<std::string>& values);
    void add_property_bytes(const std::string& name, const void* data, std::size_t size);

    // Memory reservation block entries
    void add_mem_reserve(std::uint64_t address, std::uint64_t size);

    // Build the finalized DTB binary blob
    [[nodiscard]] std::vector<std::uint8_t> build();

    // High-level Platform DTB Generator
    [[nodiscard]] static std::vector<std::uint8_t> build_platform_dtb(
        const VirtualPlatformConfig& config,
        const std::string& bootargs = "console=ttyAMA0 earlycon=pl011,0x09000000 root=/dev/ram0 rw",
        std::uint64_t initrd_start = 0,
        std::uint64_t initrd_end = 0);

private:
    std::shared_ptr<FdtNode> root_;
    std::vector<FdtNode*> node_stack_;
    std::vector<FdtMemReserveEntry> mem_reserves_;
};

// -----------------------------------------------------------------------------
// FdtReader: Parses and queries DTB binary blobs
// -----------------------------------------------------------------------------
class FdtReader {
public:
    FdtReader() = default;
    explicit FdtReader(const std::vector<std::uint8_t>& dtb);
    explicit FdtReader(const void* data, std::size_t size);

    [[nodiscard]] bool is_valid() const noexcept { return is_valid_; }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_; }

    [[nodiscard]] std::uint32_t total_size() const noexcept { return total_size_; }
    [[nodiscard]] std::uint32_t version() const noexcept { return version_; }
    [[nodiscard]] std::uint32_t boot_cpuid() const noexcept { return boot_cpuid_; }

    [[nodiscard]] const std::vector<FdtMemReserveEntry>& mem_reserves() const noexcept {
        return mem_reserves_;
    }

    [[nodiscard]] std::shared_ptr<FdtNode> root() const noexcept { return root_; }

    // Path queries, e.g. "/chosen", "/cpus/cpu@0", "/memory@40000000"
    [[nodiscard]] const FdtNode* find_node_by_path(std::string_view path) const;
    [[nodiscard]] const FdtProperty* find_property_by_path(std::string_view path, std::string_view prop_name) const;

    [[nodiscard]] std::string get_string_property(std::string_view path, std::string_view prop_name, const std::string& default_val = "") const;
    [[nodiscard]] std::uint32_t get_u32_property(std::string_view path, std::string_view prop_name, std::uint32_t default_val = 0) const;
    [[nodiscard]] std::uint64_t get_u64_property(std::string_view path, std::string_view prop_name, std::uint64_t default_val = 0) const;

private:
    bool parse(const std::uint8_t* data, std::size_t size);

    bool is_valid_{false};
    std::string error_;
    std::uint32_t total_size_{0};
    std::uint32_t version_{0};
    std::uint32_t boot_cpuid_{0};
    std::vector<FdtMemReserveEntry> mem_reserves_;
    std::shared_ptr<FdtNode> root_;
};

} // namespace winios::device
