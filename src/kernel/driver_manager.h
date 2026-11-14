#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "device/virtual_uart_device.h"

namespace winios::kernel {

class DeviceDriver {
public:
    virtual ~DeviceDriver() = default;

    virtual bool probe() = 0;
    virtual bool initialize() = 0;
    virtual std::uint64_t read_mmio(std::uint64_t offset, unsigned width) = 0;
    virtual void write_mmio(std::uint64_t offset, std::uint64_t value, unsigned width) = 0;
    virtual void handle_irq() = 0;

    [[nodiscard]] virtual std::uint64_t mmio_base() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t mmio_size() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t irq_number() const noexcept = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual bool is_initialized() const noexcept = 0;
};

class SyntheticUartDriver : public DeviceDriver {
public:
    explicit SyntheticUartDriver(
        std::uint64_t mmio_base = device::VirtualUartDevice::kDefaultUartBase,
        std::uint64_t mmio_size = device::VirtualUartDevice::kDefaultUartSize,
        std::uint32_t irq = device::VirtualUartDevice::kDefaultUartIrq);
    ~SyntheticUartDriver() override = default;

    bool probe() override;
    bool initialize() override;
    std::uint64_t read_mmio(std::uint64_t offset, unsigned width) override;
    void write_mmio(std::uint64_t offset, std::uint64_t value, unsigned width) override;
    void handle_irq() override;

    [[nodiscard]] std::uint64_t mmio_base() const noexcept override { return mmio_base_; }
    [[nodiscard]] std::uint64_t mmio_size() const noexcept override { return mmio_size_; }
    [[nodiscard]] std::uint32_t irq_number() const noexcept override { return irq_; }
    [[nodiscard]] std::string name() const override { return "SyntheticUartDriver"; }
    [[nodiscard]] bool is_initialized() const noexcept override { return initialized_; }

    // High-level console character I/O
    void write_char(char c);
    void write_string(const std::string& s);
    void inject_input(const std::string& input);
    [[nodiscard]] std::string get_console_output() const;
    void clear_console_output();

    [[nodiscard]] std::size_t irq_handled_count() const noexcept { return irq_count_; }
    [[nodiscard]] device::VirtualUartDevice& underlying_device() noexcept { return uart_; }

private:
    std::uint64_t mmio_base_;
    std::uint64_t mmio_size_;
    std::uint32_t irq_;
    bool initialized_{false};
    std::size_t irq_count_{0};
    device::VirtualUartDevice uart_;
};

class DriverManager {
public:
    DriverManager() = default;
    ~DriverManager() = default;

    bool register_driver(std::unique_ptr<DeviceDriver> driver);
    bool probe_and_initialize_all();

    [[nodiscard]] DeviceDriver* find_driver_by_address(std::uint64_t paddr);
    [[nodiscard]] DeviceDriver* find_driver_by_name(const std::string& name);
    [[nodiscard]] DeviceDriver* find_driver_by_irq(std::uint32_t irq);

    bool read_mmio(std::uint64_t paddr, unsigned width, std::uint64_t& out_value);
    bool write_mmio(std::uint64_t paddr, std::uint64_t value, unsigned width);
    bool dispatch_irq(std::uint32_t irq);

    [[nodiscard]] std::size_t driver_count() const noexcept { return drivers_.size(); }

private:
    std::vector<std::unique_ptr<DeviceDriver>> drivers_;
};

} // namespace winios::kernel
