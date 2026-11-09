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
// IOKit Well-Known Planes (Architectural standard in Darwin XNU)
// -----------------------------------------------------------------------------
inline constexpr std::string_view kIOServicePlane = "gIOServicePlane";
inline constexpr std::string_view kIODTPlane      = "gIODTPlane";
inline constexpr std::string_view kIOPowerPlane   = "gIOPowerPlane";

// -----------------------------------------------------------------------------
// OSObject: Foundational reference-counted base object in IOKit runtime
// -----------------------------------------------------------------------------
class OSObject {
public:
    virtual ~OSObject() = default;

    void retain() noexcept { ++retain_count_; }
    void release() noexcept {
        if (retain_count_ > 0) --retain_count_;
    }
    [[nodiscard]] std::uint32_t retain_count() const noexcept { return retain_count_; }

private:
    std::uint32_t retain_count_{1};
};

// -----------------------------------------------------------------------------
// IORegistryEntry: Node in one or more IORegistry planes
// -----------------------------------------------------------------------------
class IORegistryEntry : public OSObject {
public:
    explicit IORegistryEntry(std::string name = "");
    ~IORegistryEntry() override = default;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string name) { name_ = std::move(name); }

    [[nodiscard]] const std::string& location() const noexcept { return location_; }
    void set_location(std::string loc) { location_ = std::move(loc); }

    // Properties (dictionary of key-value attributes)
    void set_property(std::string key, std::vector<std::uint8_t> value);
    void set_property_string(std::string key, std::string value);
    void set_property_u32(std::string key, std::uint32_t value);
    void set_property_u64(std::string key, std::uint64_t value);

    [[nodiscard]] bool has_property(std::string_view key) const noexcept;
    [[nodiscard]] const std::vector<std::uint8_t>* get_property(std::string_view key) const noexcept;
    [[nodiscard]] std::string get_property_string(std::string_view key, const std::string& default_val = "") const;
    [[nodiscard]] std::uint32_t get_property_u32(std::string_view key, std::uint32_t default_val = 0) const noexcept;
    [[nodiscard]] std::uint64_t get_property_u64(std::string_view key, std::uint64_t default_val = 0) const noexcept;
    [[nodiscard]] const std::map<std::string, std::vector<std::uint8_t>>& properties() const noexcept { return properties_; }

    // Plane Links (Parents & Children per Plane)
    void attach_to_plane(std::string_view plane, IORegistryEntry* parent);
    void detach_from_plane(std::string_view plane);

    [[nodiscard]] IORegistryEntry* get_parent(std::string_view plane) const noexcept;
    [[nodiscard]] const std::vector<IORegistryEntry*>& get_children(std::string_view plane) const noexcept;
    [[nodiscard]] std::string get_path(std::string_view plane) const;

    [[nodiscard]] bool in_plane(std::string_view plane) const noexcept;

private:
    std::string name_;
    std::string location_;
    std::map<std::string, std::vector<std::uint8_t>> properties_;

    // Plane linkage maps
    std::map<std::string, IORegistryEntry*> parents_;
    std::map<std::string, std::vector<IORegistryEntry*>> children_;
};

// -----------------------------------------------------------------------------
// IOService: Life-cycle managed driver/device service object
// -----------------------------------------------------------------------------
enum class IOServiceState {
    Registered,
    Probed,
    Active,
    Terminated
};

class IOService : public IORegistryEntry {
public:
    explicit IOService(std::string name = "");
    ~IOService() override = default;

    [[nodiscard]] IOServiceState service_state() const noexcept { return state_; }
    void set_service_state(IOServiceState st) noexcept { state_ = st; }

    [[nodiscard]] const std::string& io_class() const noexcept { return io_class_; }
    void set_io_class(std::string cls) { io_class_ = std::move(cls); }

    [[nodiscard]] std::int32_t probe_score() const noexcept { return probe_score_; }
    void set_probe_score(std::int32_t score) noexcept { probe_score_ = score; }

    // Service lifecycle
    virtual bool init();
    virtual std::int32_t probe(IOService* provider, std::int32_t* score);
    virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);

private:
    IOServiceState state_{IOServiceState::Registered};
    std::string io_class_;
    std::int32_t probe_score_{0};
};

// -----------------------------------------------------------------------------
// IOPlatformDevice: Hardware device mapped to an SoC MMIO region & IRQs
// -----------------------------------------------------------------------------
class IOPlatformDevice : public IOService {
public:
    explicit IOPlatformDevice(std::string name = "");
    ~IOPlatformDevice() override = default;

    [[nodiscard]] std::uint64_t mmio_base() const noexcept { return mmio_base_; }
    [[nodiscard]] std::uint64_t mmio_size() const noexcept { return mmio_size_; }
    void set_mmio(std::uint64_t base, std::uint64_t size) noexcept {
        mmio_base_ = base;
        mmio_size_ = size;
    }

    [[nodiscard]] std::uint32_t irq_line() const noexcept { return irq_line_; }
    void set_irq_line(std::uint32_t irq) noexcept { irq_line_ = irq; }

    [[nodiscard]] const std::vector<std::string>& compatible_tokens() const noexcept { return compatible_tokens_; }
    void set_compatible_tokens(std::vector<std::string> tokens) { compatible_tokens_ = std::move(tokens); }

private:
    std::uint64_t mmio_base_{0};
    std::uint64_t mmio_size_{0};
    std::uint32_t irq_line_{0};
    std::vector<std::string> compatible_tokens_;
};

// -----------------------------------------------------------------------------
// IOPlatformExpertDevice: Root system platform device
// -----------------------------------------------------------------------------
class IOPlatformExpertDevice : public IOPlatformDevice {
public:
    explicit IOPlatformExpertDevice(std::string name = "IOPlatformExpertDevice");
    ~IOPlatformExpertDevice() override = default;
};

// -----------------------------------------------------------------------------
// IORegistryManager: Manages planes and tree construction
// -----------------------------------------------------------------------------
class IORegistryManager {
public:
    IORegistryManager();
    ~IORegistryManager() = default;

    void reset();

    // Plane Roots
    [[nodiscard]] IORegistryEntry* get_plane_root(std::string_view plane) const noexcept;

    // Entry Management
    IORegistryEntry* create_entry(std::string name);
    IOPlatformDevice* create_platform_device(std::string name);

    [[nodiscard]] IORegistryEntry* lookup_by_path(std::string_view plane, std::string_view path) const;
    [[nodiscard]] const std::vector<std::unique_ptr<IORegistryEntry>>& all_entries() const noexcept { return entries_; }

    // Construction from AppleDeviceTree
    bool build_from_devicetree(const device::AppleDeviceTreeNode& dt_root, std::string* out_error = nullptr);

    // Matching
    [[nodiscard]] std::size_t count_entries_in_plane(std::string_view plane) const noexcept;

private:
    void traverse_dt_node(const device::AppleDeviceTreeNode& dt_node,
                          IORegistryEntry* dt_parent,
                          IORegistryEntry* service_parent);

    std::vector<std::unique_ptr<IORegistryEntry>> entries_;
    std::unique_ptr<IORegistryEntry> service_root_;
    std::unique_ptr<IORegistryEntry> dt_root_;
    std::unique_ptr<IORegistryEntry> power_root_;
};

} // namespace winios::runtime
