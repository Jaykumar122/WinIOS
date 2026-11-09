#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "device/apple_devicetree.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// IOKitPersonality: Darwin driver personality specification from driver catalog
// -----------------------------------------------------------------------------
struct IOKitPersonality {
    std::string io_class;                   // e.g. "AppleA16InterruptController"
    std::string provider_class;             // e.g. "IOPlatformDevice", "IOPlatformExpertDevice"
    std::vector<std::string> name_matches;  // Matches DeviceTree node name
    std::vector<std::string> compatible_matches; // Matches DeviceTree compatible property tokens
    std::map<std::string, std::string> property_matches; // Specific property key-value constraints
    std::int32_t probe_score{1000};         // Candidate arbitration ranking
};

// -----------------------------------------------------------------------------
// IOKitDriverState: Lifecycle state of a matched Darwin driver instance
// -----------------------------------------------------------------------------
enum class IOKitDriverState {
    Unmatched,
    Probed,
    Active,
    Failed
};

// -----------------------------------------------------------------------------
// IOKitMatchedDriver: Instantiated and matched driver bound to a hardware node
// -----------------------------------------------------------------------------
struct IOKitMatchedDriver {
    IOKitPersonality personality;
    std::string device_path;
    std::string device_name;
    std::uint64_t mmio_base{0};
    std::uint64_t mmio_size{0};
    std::uint32_t irq_line{0};
    std::int32_t final_score{0};
    IOKitDriverState state{IOKitDriverState::Unmatched};
    std::string status_info;
};

// -----------------------------------------------------------------------------
// IORegistryDevice: Registered node in the Darwin IORegistry tree
// -----------------------------------------------------------------------------
struct IORegistryDevice {
    std::string path;
    std::string name;
    std::string device_type;
    std::vector<std::string> compatible_tokens;
    std::uint64_t mmio_base{0};
    std::uint64_t mmio_size{0};
    std::uint32_t irq_line{0};
    std::map<std::string, std::vector<std::uint8_t>> raw_properties;
    std::shared_ptr<IOKitMatchedDriver> matched_driver;
};

// -----------------------------------------------------------------------------
// DarwinPlatformExpert: IOKit Platform Expert, DeviceTree Probing & Driver Matcher
// -----------------------------------------------------------------------------
class DarwinPlatformExpert {
public:
    DarwinPlatformExpert();
    ~DarwinPlatformExpert() = default;

    DarwinPlatformExpert(const DarwinPlatformExpert&) = default;
    DarwinPlatformExpert& operator=(const DarwinPlatformExpert&) = default;
    DarwinPlatformExpert(DarwinPlatformExpert&&) noexcept = default;
    DarwinPlatformExpert& operator=(DarwinPlatformExpert&&) noexcept = default;

    // Driver Catalog Management
    void register_personality(const IOKitPersonality& personality);
    void load_default_iphone15_personalities();
    void clear_catalog() noexcept;

    // Platform Probing & Driver Matching Execution
    bool probe_and_match(const device::AppleDeviceTreeNode& root_node, std::string& out_error);
    bool probe_and_match_adts(const void* adts_data, std::size_t size, std::string& out_error);

    // Queries & Inspection
    [[nodiscard]] const std::vector<IOKitPersonality>& catalog() const noexcept { return catalog_; }
    [[nodiscard]] const std::vector<IORegistryDevice>& registry() const noexcept { return registry_; }
    [[nodiscard]] const std::vector<IOKitMatchedDriver>& matched_drivers() const noexcept { return matched_drivers_; }

    [[nodiscard]] const IOKitMatchedDriver* find_driver_by_class(std::string_view io_class) const noexcept;
    [[nodiscard]] const IOKitMatchedDriver* find_driver_by_path(std::string_view device_path) const noexcept;
    [[nodiscard]] const IORegistryDevice* find_device_by_path(std::string_view path) const noexcept;

    // Platform readiness and peripheral verification
    [[nodiscard]] bool verify_platform_readiness(std::string& out_diagnostic) const;

private:
    void traverse_devicetree_node(
        const device::AppleDeviceTreeNode& node,
        const std::string& parent_path);

    [[nodiscard]] std::shared_ptr<IOKitMatchedDriver> match_device(
        const IORegistryDevice& dev);

    std::vector<IOKitPersonality> catalog_;
    std::vector<IORegistryDevice> registry_;
    std::vector<IOKitMatchedDriver> matched_drivers_;
    std::string platform_model_;
    std::string target_type_;
    std::uint32_t board_id_{0};
    std::uint32_t chip_id_{0};
};

} // namespace winios::runtime
