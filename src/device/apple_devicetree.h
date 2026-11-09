#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// Apple DeviceTree (ADTS) Constants
// -----------------------------------------------------------------------------
constexpr std::size_t kAppleDtPropertyMaxName = 32;

#pragma pack(push, 1)
struct AppleDtEntryHeader {
    std::uint32_t nProperties{0};
    std::uint32_t nChildren{0};
};

struct AppleDtPropertyHeader {
    char name[kAppleDtPropertyMaxName]{0}; // Fixed 32-byte null-padded name
    std::uint32_t length{0};              // Byte length of raw value
};
#pragma pack(pop)

// -----------------------------------------------------------------------------
// AppleDeviceTreeProperty: Individual named property
// -----------------------------------------------------------------------------
struct AppleDeviceTreeProperty {
    std::string name;
    std::vector<std::uint8_t> data;
};

// -----------------------------------------------------------------------------
// AppleDeviceTreeNode: Hierarchical node with properties and children
// -----------------------------------------------------------------------------
class AppleDeviceTreeNode {
public:
    explicit AppleDeviceTreeNode(std::string name = "");
    ~AppleDeviceTreeNode() = default;

    AppleDeviceTreeNode(const AppleDeviceTreeNode&) = delete;
    AppleDeviceTreeNode& operator=(const AppleDeviceTreeNode&) = delete;

    AppleDeviceTreeNode(AppleDeviceTreeNode&&) noexcept = default;
    AppleDeviceTreeNode& operator=(AppleDeviceTreeNode&&) noexcept = default;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    // Property manipulation
    void add_property_empty(const std::string& prop_name);
    void add_property_u32(const std::string& prop_name, std::uint32_t val);
    void add_property_u64(const std::string& prop_name, std::uint64_t val);
    void add_property_string(const std::string& prop_name, const std::string& str);
    void add_property_bytes(const std::string& prop_name, const void* data, std::size_t size);

    [[nodiscard]] const AppleDeviceTreeProperty* find_property(std::string_view prop_name) const noexcept;
    [[nodiscard]] const std::vector<AppleDeviceTreeProperty>& properties() const noexcept { return properties_; }

    // Children manipulation
    AppleDeviceTreeNode* add_child(std::unique_ptr<AppleDeviceTreeNode> child);
    AppleDeviceTreeNode* add_child_node(std::string child_name);

    [[nodiscard]] AppleDeviceTreeNode* find_child(std::string_view child_name) const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<AppleDeviceTreeNode>>& children() const noexcept { return children_; }

    // Path traversal (e.g. "/arm-io/aic" or "arm-io/aic")
    [[nodiscard]] const AppleDeviceTreeNode* find_path(std::string_view path) const noexcept;

private:
    std::string name_;
    std::vector<AppleDeviceTreeProperty> properties_;
    std::vector<std::unique_ptr<AppleDeviceTreeNode>> children_;
};

// -----------------------------------------------------------------------------
// AppleDeviceTreeBuilder: Serializes and deserializes the binary Apple DeviceTree
// -----------------------------------------------------------------------------
class AppleDeviceTreeBuilder {
public:
    AppleDeviceTreeBuilder() = default;
    ~AppleDeviceTreeBuilder() = default;

    // Binary serialization of node hierarchy into little-endian ADTS stream
    [[nodiscard]] static std::vector<std::uint8_t> serialize(const AppleDeviceTreeNode& root);

    // Binary deserialization from raw ADTS stream
    [[nodiscard]] static std::unique_ptr<AppleDeviceTreeNode> deserialize(
        const void* data,
        std::size_t size,
        std::string* out_error = nullptr);

    // Profile generator for iPhone 15 (iPhone15,4 / D37AP / A16 Bionic / T8120)
    [[nodiscard]] static std::vector<std::uint8_t> create_iphone15_devicetree(
        std::uint64_t ram_base = 0x40000000ULL,
        std::uint64_t ram_size = 6ULL * 1024 * 1024 * 1024,
        const std::string& boot_args = "-v debug=0x144 serial=3",
        std::uint64_t framebuffer_base = 0x50000000ULL,
        std::uint64_t ramdisk_base = 0,
        std::uint64_t ramdisk_size = 0);

    // Profile generator for iPhone 14 Pro (iPhone15,2 / D73AP / A16 Bionic / T8120)
    [[nodiscard]] static std::vector<std::uint8_t> create_iphone14_pro_devicetree(
        std::uint64_t ram_base = 0x40000000ULL,
        std::uint64_t ram_size = 6ULL * 1024 * 1024 * 1024,
        const std::string& boot_args = "-v debug=0x144 serial=3",
        std::uint64_t framebuffer_base = 0x50000000ULL,
        std::uint64_t ramdisk_base = 0,
        std::uint64_t ramdisk_size = 0);
};

} // namespace winios::device
