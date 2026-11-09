#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/virtual_time.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"

namespace winios::device {

// -----------------------------------------------------------------------------
// VirtualPlatformConfig: Architecture configuration for the virtual platform
// -----------------------------------------------------------------------------
struct VirtualPlatformConfig {
    std::size_t core_count{1};
    std::uint64_t ram_base{0x40000000ULL};
    std::size_t ram_size{0x04000000}; // 64 MB default
    std::uint64_t boot_entry{0x40000000ULL};

    // Peripheral addresses
    std::uint64_t gicd_base{0x08000000ULL};
    std::uint64_t gicr_base{0x080A0000ULL};
    std::uint64_t uart_base{0x09000000ULL};
    std::uint64_t dma_base{0x10000000ULL};

    // Peripheral IRQs
    std::uint32_t uart_irq{33u};
    std::uint32_t dma_irq{34u};
};

// -----------------------------------------------------------------------------
// VirtualPlatform: Integrated virtual machine platform topology
// -----------------------------------------------------------------------------
class VirtualPlatform {
public:
    explicit VirtualPlatform(const VirtualPlatformConfig& config = {});
    ~VirtualPlatform();

    VirtualPlatform(const VirtualPlatform&) = delete;
    VirtualPlatform& operator=(const VirtualPlatform&) = delete;

    // Topology Accessors
    [[nodiscard]] const VirtualPlatformConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::size_t core_count() const noexcept { return cpus_.size(); }
    [[nodiscard]] CPU& get_cpu(std::size_t index);
    [[nodiscard]] const CPU& get_cpu(std::size_t index) const;

    [[nodiscard]] Memory& memory() noexcept { return *memory_; }
    [[nodiscard]] const Memory& memory() const noexcept { return *memory_; }

    [[nodiscard]] GICv3& gic() noexcept { return gic_; }
    [[nodiscard]] const GICv3& gic() const noexcept { return gic_; }

    [[nodiscard]] VirtualTime& virtual_time() noexcept { return virtual_time_; }
    [[nodiscard]] const VirtualTime& virtual_time() const noexcept { return virtual_time_; }

    [[nodiscard]] std::shared_ptr<VirtualDeviceBus> device_bus() const noexcept { return device_bus_; }
    [[nodiscard]] std::shared_ptr<VirtualUartDevice> uart() const noexcept { return uart_; }
    [[nodiscard]] std::shared_ptr<VirtualDmaDevice> dma() const noexcept { return dma_; }

    // Platform Lifecycle
    void reset(std::uint64_t entry_point = 0);
    bool is_core_active(std::size_t index) const noexcept;
    void set_core_active(std::size_t index, bool active) noexcept;

    // Execution Stepping
    bool step_core(std::size_t core_id);
    std::size_t step_round_robin();
    std::size_t run_deterministic(std::size_t max_total_instructions);

    // Platform State Hashing
    [[nodiscard]] std::uint64_t compute_platform_hash() const;

    // Validation
    [[nodiscard]] bool validate_memory_map() const noexcept;

private:
    VirtualPlatformConfig config_;
    std::vector<std::unique_ptr<CPU>> cpus_;
    std::vector<bool> core_active_;
    std::unique_ptr<Memory> memory_;
    GICv3 gic_;
    VirtualTime virtual_time_;

    std::shared_ptr<VirtualDeviceBus> device_bus_;
    std::shared_ptr<VirtualUartDevice> uart_;
    std::shared_ptr<VirtualDmaDevice> dma_;
};

} // namespace winios::device
