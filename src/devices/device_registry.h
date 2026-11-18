#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <optional>

namespace winios::devices {

class DeviceRegistry {
public:
    static DeviceRegistry& get();

    DeviceRegistry() = default;
    ~DeviceRegistry() = default;

    DeviceRegistry(const DeviceRegistry&) = delete;
    DeviceRegistry& operator=(const DeviceRegistry&) = delete;

    // Registration and lifecycle
    bool register_device(std::shared_ptr<IVirtualDevice> device);
    bool unregister_device(std::uint32_t device_id);
    bool unregister_device_by_handle(std::uint64_t handle);

    // Lookup
    [[nodiscard]] std::shared_ptr<IVirtualDevice> get_device(std::uint32_t device_id) const;
    [[nodiscard]] std::shared_ptr<IVirtualDevice> get_by_handle(std::uint64_t handle) const;
    [[nodiscard]] bool has_device(std::uint32_t device_id) const;
    [[nodiscard]] bool is_handle_valid(std::uint64_t handle) const;

    // Enumeration
    [[nodiscard]] std::vector<DeviceInfo> enumerate_all() const;
    [[nodiscard]] std::vector<DeviceInfo> enumerate_by_class(DeviceClass cls) const;
    [[nodiscard]] std::size_t device_count() const;
    [[nodiscard]] std::size_t device_count_by_class(DeviceClass cls) const;

    // Lifecycle Broadcast
    void initialize_all();
    void start_all();
    void stop_all();
    void suspend_all();
    void resume_all();

    // Reset
    void reset();

private:
    mutable std::mutex lock_;
    std::unordered_map<std::uint32_t, std::shared_ptr<IVirtualDevice>> devices_;
    std::unordered_map<std::uint32_t, std::uint32_t> generations_;
    std::uint64_t total_registered_{0};
    std::uint64_t total_unregistered_{0};
};

} // namespace winios::devices
