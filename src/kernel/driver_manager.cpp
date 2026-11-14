#include "kernel/driver_manager.h"

namespace winios::kernel {

SyntheticUartDriver::SyntheticUartDriver(std::uint64_t mmio_base, std::uint64_t mmio_size, std::uint32_t irq)
    : mmio_base_(mmio_base), mmio_size_(mmio_size), irq_(irq),
      uart_(0x55415254u, mmio_base, mmio_size, irq) {}

bool SyntheticUartDriver::probe() {
    // Read Flag Register or Line Control Register to verify presence
    const std::uint64_t fr = uart_.read_register(device::VirtualUartDevice::kRegFR, 4);
    // FR should have TXFE (bit 7) and RXFE (bit 4) asserted initially
    return (fr & device::VirtualUartDevice::kFR_TXFE) != 0;
}

bool SyntheticUartDriver::initialize() {
    uart_.reset();
    // Enable UART, TX, and RX
    uart_.write_register(
        device::VirtualUartDevice::kRegCR,
        device::VirtualUartDevice::kCR_UARTEN |
        device::VirtualUartDevice::kCR_TXE |
        device::VirtualUartDevice::kCR_RXE,
        4);
    initialized_ = true;
    return true;
}

std::uint64_t SyntheticUartDriver::read_mmio(std::uint64_t offset, unsigned width) {
    return uart_.read_register(offset, width);
}

void SyntheticUartDriver::write_mmio(std::uint64_t offset, std::uint64_t value, unsigned width) {
    uart_.write_register(offset, value, width);
}

void SyntheticUartDriver::handle_irq() {
    ++irq_count_;
    // Acknowledge/clear interrupt in ICR
    uart_.write_register(device::VirtualUartDevice::kRegICR, 0x7FFu, 4);
}

void SyntheticUartDriver::write_char(char c) {
    uart_.write_register(device::VirtualUartDevice::kRegDR, static_cast<std::uint8_t>(c), 4);
}

void SyntheticUartDriver::write_string(const std::string& s) {
    for (char c : s) {
        write_char(c);
    }
}

void SyntheticUartDriver::inject_input(const std::string& input) {
    uart_.inject_input(input);
}

std::string SyntheticUartDriver::get_console_output() const {
    return uart_.get_output_text();
}

void SyntheticUartDriver::clear_console_output() {
    uart_.clear_output();
}

bool DriverManager::register_driver(std::unique_ptr<DeviceDriver> driver) {
    if (!driver) {
        return false;
    }
    drivers_.push_back(std::move(driver));
    return true;
}

bool DriverManager::probe_and_initialize_all() {
    bool all_ok = true;
    for (auto& driver : drivers_) {
        if (driver->probe()) {
            if (!driver->initialize()) {
                all_ok = false;
            }
        } else {
            all_ok = false;
        }
    }
    return all_ok;
}

DeviceDriver* DriverManager::find_driver_by_address(std::uint64_t paddr) {
    for (auto& driver : drivers_) {
        const std::uint64_t base = driver->mmio_base();
        const std::uint64_t size = driver->mmio_size();
        if (paddr >= base && paddr < base + size) {
            return driver.get();
        }
    }
    return nullptr;
}

DeviceDriver* DriverManager::find_driver_by_name(const std::string& name) {
    for (auto& driver : drivers_) {
        if (driver->name() == name) {
            return driver.get();
        }
    }
    return nullptr;
}

DeviceDriver* DriverManager::find_driver_by_irq(std::uint32_t irq) {
    for (auto& driver : drivers_) {
        if (driver->irq_number() == irq) {
            return driver.get();
        }
    }
    return nullptr;
}

bool DriverManager::read_mmio(std::uint64_t paddr, unsigned width, std::uint64_t& out_value) {
    DeviceDriver* drv = find_driver_by_address(paddr);
    if (!drv) {
        return false;
    }
    const std::uint64_t offset = paddr - drv->mmio_base();
    out_value = drv->read_mmio(offset, width);
    return true;
}

bool DriverManager::write_mmio(std::uint64_t paddr, std::uint64_t value, unsigned width) {
    DeviceDriver* drv = find_driver_by_address(paddr);
    if (!drv) {
        return false;
    }
    const std::uint64_t offset = paddr - drv->mmio_base();
    drv->write_mmio(offset, value, width);
    return true;
}

bool DriverManager::dispatch_irq(std::uint32_t irq) {
    DeviceDriver* drv = find_driver_by_irq(irq);
    if (!drv) {
        return false;
    }
    drv->handle_irq();
    return true;
}

} // namespace winios::kernel
