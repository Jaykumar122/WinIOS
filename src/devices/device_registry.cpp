#include "devices/device_registry.h"

namespace winios::devices {

DeviceRegistry& DeviceRegistry::get() {
    static DeviceRegistry instance;
    return instance;
}

bool DeviceRegistry::register_device(std::shared_ptr<IVirtualDevice> device) {
    if (!device) return false;
    std::lock_guard<std::mutex> guard(lock_);

    const std::uint32_t id = device->device_id();
    if (id == 0) return false;

    // Reject duplicate ID
    if (devices_.find(id) != devices_.end()) {
        return false;
    }

    auto gen_it = generations_.find(id);
    if (gen_it != generations_.end()) {
        while (device->generation() < gen_it->second) {
            device->advance_generation();
        }
    }

    devices_[id] = device;
    generations_[id] = device->generation();
    ++total_registered_;
    return true;
}

bool DeviceRegistry::unregister_device(std::uint32_t device_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = devices_.find(device_id);
    if (it == devices_.end()) {
        return false;
    }

    it->second->stop();
    it->second->mark_removed();
    // Advance generation in map so stale handles fail
    generations_[device_id] = it->second->generation() + 1;
    devices_.erase(it);
    ++total_unregistered_;
    return true;
}

bool DeviceRegistry::unregister_device_by_handle(std::uint64_t handle) {
    const std::uint32_t id = device_handle_id(handle);
    const std::uint32_t gen = device_handle_gen(handle);

    std::lock_guard<std::mutex> guard(lock_);
    auto it = devices_.find(id);
    if (it == devices_.end()) {
        return false;
    }

    if (it->second->generation() != gen) {
        return false; // Stale handle rejection
    }

    it->second->stop();
    it->second->mark_removed();
    generations_[id] = gen + 1;
    devices_.erase(it);
    ++total_unregistered_;
    return true;
}

std::shared_ptr<IVirtualDevice> DeviceRegistry::get_device(std::uint32_t device_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = devices_.find(device_id);
    if (it != devices_.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<IVirtualDevice> DeviceRegistry::get_by_handle(std::uint64_t handle) const {
    const std::uint32_t id = device_handle_id(handle);
    const std::uint32_t gen = device_handle_gen(handle);

    std::lock_guard<std::mutex> guard(lock_);
    auto it = devices_.find(id);
    if (it != devices_.end()) {
        if (it->second->generation() == gen) {
            return it->second;
        }
    }
    return nullptr;
}

bool DeviceRegistry::has_device(std::uint32_t device_id) const {
    std::lock_guard<std::mutex> guard(lock_);
    return devices_.find(device_id) != devices_.end();
}

bool DeviceRegistry::is_handle_valid(std::uint64_t handle) const {
    return get_by_handle(handle) != nullptr;
}

std::vector<DeviceInfo> DeviceRegistry::enumerate_all() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<DeviceInfo> result;
    result.reserve(devices_.size());
    for (const auto& [_, dev] : devices_) {
        if (dev) {
            result.push_back(dev->get_info());
        }
    }
    return result;
}

std::vector<DeviceInfo> DeviceRegistry::enumerate_by_class(DeviceClass cls) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<DeviceInfo> result;
    for (const auto& [_, dev] : devices_) {
        if (dev && dev->device_class() == cls) {
            result.push_back(dev->get_info());
        }
    }
    return result;
}

std::size_t DeviceRegistry::device_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return devices_.size();
}

std::size_t DeviceRegistry::device_count_by_class(DeviceClass cls) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t cnt = 0;
    for (const auto& [_, dev] : devices_) {
        if (dev && dev->device_class() == cls) {
            ++cnt;
        }
    }
    return cnt;
}

void DeviceRegistry::initialize_all() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->initialize();
    }
}

void DeviceRegistry::start_all() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->start();
    }
}

void DeviceRegistry::stop_all() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->stop();
    }
}

void DeviceRegistry::suspend_all() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->suspend();
    }
}

void DeviceRegistry::resume_all() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->resume();
    }
}

void DeviceRegistry::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto& [_, dev] : devices_) {
        if (dev) dev->stop();
    }
    devices_.clear();
    generations_.clear();
    total_registered_ = 0;
    total_unregistered_ = 0;
}

} // namespace winios::devices
