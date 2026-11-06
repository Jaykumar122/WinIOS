#include "device/virtual_device.h"

#include <stdexcept>

namespace winios::device {

VirtualDevice::VirtualDevice(std::uint32_t device_id,
                             std::string name,
                             VirtualDeviceType type,
                             std::uint64_t mmio_base,
                             std::uint64_t mmio_size,
                             std::uint32_t irq_line)
    : device_id_(device_id),
      name_(std::move(name)),
      type_(type),
      mmio_base_(mmio_base),
      mmio_size_(mmio_size),
      irq_line_(irq_line),
      status_(VirtualDeviceStatus::Idle) {
    if (mmio_size == 0) {
        throw std::invalid_argument("VirtualDevice MMIO size must be greater than zero");
    }
}

void VirtualDevice::set_enabled(bool enabled) noexcept {
    if (enabled) {
        if (status_ == VirtualDeviceStatus::Disabled || status_ == VirtualDeviceStatus::Uninitialized) {
            status_ = VirtualDeviceStatus::Idle;
        }
    } else {
        status_ = VirtualDeviceStatus::Disabled;
    }
}

} // namespace winios::device
