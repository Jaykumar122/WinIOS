#pragma once

#include "device/virtual_device_types.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualDevice: Base abstraction for all memory-mapped virtual devices
// -----------------------------------------------------------------------------
class VirtualDevice {
public:
    using InterruptCallback = std::function<void(std::uint32_t irq_line, bool asserted)>;

    VirtualDevice(std::uint32_t device_id,
                  std::string name,
                  VirtualDeviceType type,
                  std::uint64_t mmio_base,
                  std::uint64_t mmio_size,
                  std::uint32_t irq_line = 0);

    virtual ~VirtualDevice() = default;

    // Device identification and geometry
    [[nodiscard]] std::uint32_t device_id() const noexcept { return device_id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] VirtualDeviceType type() const noexcept { return type_; }
    [[nodiscard]] std::uint64_t mmio_base() const noexcept { return mmio_base_; }
    [[nodiscard]] std::uint64_t mmio_size() const noexcept { return mmio_size_; }
    [[nodiscard]] std::uint32_t irq_line() const noexcept { return irq_line_; }

    // Status & enablement
    [[nodiscard]] VirtualDeviceStatus status() const noexcept { return status_; }
    void set_status(VirtualDeviceStatus status) noexcept { status_ = status; }
    [[nodiscard]] bool is_enabled() const noexcept { return status_ != VirtualDeviceStatus::Disabled && status_ != VirtualDeviceStatus::Uninitialized; }
    void set_enabled(bool enabled) noexcept;
    [[nodiscard]] virtual bool is_busy() const noexcept { return status_ == VirtualDeviceStatus::Active; }

    // Address containment test
    [[nodiscard]] bool contains_address(std::uint64_t pa) const noexcept {
        return pa >= mmio_base_ && pa < mmio_base_ + mmio_size_;
    }

    // Register MMIO read/write interface
    [[nodiscard]] virtual std::uint64_t read_register(std::uint64_t offset, unsigned width) = 0;
    virtual void write_register(std::uint64_t offset, std::uint64_t value, unsigned width) = 0;

    // Reset device to initial power-on state
    virtual void reset() noexcept = 0;

    // State serialization for .wsnap v2 snapshots
    [[nodiscard]] virtual std::vector<std::uint8_t> serialize_state() const = 0;
    [[nodiscard]] virtual bool deserialize_state(const std::uint8_t* data, std::size_t size) = 0;

    // Interrupt signaling
    void set_interrupt_callback(InterruptCallback callback) noexcept {
        interrupt_callback_ = std::move(callback);
    }

protected:
    void trigger_interrupt(bool asserted) {
        if (interrupt_callback_) {
            interrupt_callback_(irq_line_, asserted);
        }
    }

    std::uint32_t device_id_{0};
    std::string name_;
    VirtualDeviceType type_{VirtualDeviceType::GenericMmio};
    std::uint64_t mmio_base_{0};
    std::uint64_t mmio_size_{0};
    std::uint32_t irq_line_{0};
    VirtualDeviceStatus status_{VirtualDeviceStatus::Idle};
    InterruptCallback interrupt_callback_;
};

} // namespace winios::device
