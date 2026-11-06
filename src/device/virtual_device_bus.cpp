#include "device/virtual_device_bus.h"

#include <algorithm>
#include <stdexcept>

namespace winios::device {

VirtualDeviceBus::VirtualDeviceBus(VirtualDeviceCapabilities capabilities)
    : capabilities_(capabilities) {}

bool VirtualDeviceBus::register_device(std::shared_ptr<VirtualDevice> device) {
    if (!device) return false;

    // Check for ID collision
    for (const auto& dev : devices_) {
        if (dev->device_id() == device->device_id()) {
            return false;
        }
    }

    // Check for address range collision
    const std::uint64_t new_start = device->mmio_base();
    const std::uint64_t new_end = new_start + device->mmio_size();

    for (const auto& dev : devices_) {
        const std::uint64_t ex_start = dev->mmio_base();
        const std::uint64_t ex_end = ex_start + dev->mmio_size();

        if (std::max(new_start, ex_start) < std::min(new_end, ex_end)) {
            // Overlapping MMIO address ranges
            return false;
        }
    }

    // Wire interrupt signaling to interrupt router if available
    device->set_interrupt_callback([this, dev_id = device->device_id()](std::uint32_t irq_line, bool asserted) {
        if (interrupt_router_ && irq_line > 0) {
            if (asserted) {
                // Route as interrupt to core 0 by default
                interrupt_router_->request_interrupt(0, irq_line, 0x80u);
            }
        }
    });

    devices_.push_back(device);
    return true;
}

bool VirtualDeviceBus::unregister_device(std::uint32_t device_id) {
    auto it = std::remove_if(devices_.begin(), devices_.end(), [device_id](const auto& d) {
        return d->device_id() == device_id;
    });
    if (it != devices_.end()) {
        devices_.erase(it, devices_.end());
        return true;
    }
    return false;
}

void VirtualDeviceBus::clear_devices() noexcept {
    devices_.clear();
}

std::shared_ptr<VirtualDevice> VirtualDeviceBus::find_device_by_id(std::uint32_t device_id) const noexcept {
    for (const auto& dev : devices_) {
        if (dev->device_id() == device_id) {
            return dev;
        }
    }
    return nullptr;
}

std::shared_ptr<VirtualDevice> VirtualDeviceBus::find_device_by_address(std::uint64_t pa) const noexcept {
    for (const auto& dev : devices_) {
        if (dev->contains_address(pa)) {
            return dev;
        }
    }
    return nullptr;
}

bool VirtualDeviceBus::is_device_address(std::uint64_t pa) const noexcept {
    return find_device_by_address(pa) != nullptr;
}

bool VirtualDeviceBus::check_alignment(std::uint64_t address, unsigned width) const noexcept {
    if (width == 1) return true;
    if (width == 2) return (address & 1u) == 0;
    if (width == 4) return (address & 3u) == 0;
    if (width == 8) return (address & 7u) == 0;
    return false;
}

std::uint64_t VirtualDeviceBus::read_mmio(std::uint64_t pa, unsigned width, std::uint32_t core_id, std::uint64_t ret_count) {
    VirtualDeviceAccess access;
    access.physical_address = pa;
    access.width = width;
    access.is_write = false;
    access.core_id = core_id;
    access.retirement_count = ret_count;

    if (!capabilities_.allow_virtual_devices) {
        access.status = VirtualDeviceErrorCode::DeviceDisabled;
        access_log_.push_back(access);
        return 0;
    }

    if (!check_alignment(pa, width)) {
        access.status = VirtualDeviceErrorCode::MisalignedAccess;
        access_log_.push_back(access);
        return 0;
    }

    auto dev = find_device_by_address(pa);
    if (!dev) {
        access.status = VirtualDeviceErrorCode::DeviceNotFound;
        access_log_.push_back(access);
        return 0;
    }

    if (!dev->is_enabled()) {
        access.status = VirtualDeviceErrorCode::DeviceDisabled;
        access_log_.push_back(access);
        return 0;
    }

    const std::uint64_t offset = pa - dev->mmio_base();
    const std::uint64_t val = dev->read_register(offset, width);
    access.value = val;
    access.status = VirtualDeviceErrorCode::Success;
    access_log_.push_back(access);

    if (sndl_recorder_) {
        sndl_recorder_->record_mmio_read(core_id, pa, val, width, 0);
    }

    return val;
}

void VirtualDeviceBus::write_mmio(std::uint64_t pa, std::uint64_t value, unsigned width, std::uint32_t core_id, std::uint64_t ret_count) {
    VirtualDeviceAccess access;
    access.physical_address = pa;
    access.value = value;
    access.width = width;
    access.is_write = true;
    access.core_id = core_id;
    access.retirement_count = ret_count;

    if (!capabilities_.allow_virtual_devices) {
        access.status = VirtualDeviceErrorCode::DeviceDisabled;
        access_log_.push_back(access);
        return;
    }

    if (!check_alignment(pa, width)) {
        access.status = VirtualDeviceErrorCode::MisalignedAccess;
        access_log_.push_back(access);
        return;
    }

    auto dev = find_device_by_address(pa);
    if (!dev) {
        access.status = VirtualDeviceErrorCode::DeviceNotFound;
        access_log_.push_back(access);
        return;
    }

    if (!dev->is_enabled()) {
        access.status = VirtualDeviceErrorCode::DeviceDisabled;
        access_log_.push_back(access);
        return;
    }

    const std::uint64_t offset = pa - dev->mmio_base();
    dev->write_register(offset, value, width);
    access.status = VirtualDeviceErrorCode::Success;
    access_log_.push_back(access);
}

void VirtualDeviceBus::reset_all() noexcept {
    for (auto& dev : devices_) {
        dev->reset();
    }
}

} // namespace winios::device
