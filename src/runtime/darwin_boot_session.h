#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "device/apple_aic.h"
#include "device/apple_display.h"
#include "device/apple_pmgr.h"
#include "device/apple_devicetree.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_uart_device.h"
#include "firmware/apple_boot_args.h"
#include "loader/guest_memory_map.h"
#include "loader/macho_loader.h"
#include "loader/ramdisk_loader.h"
#include "memory/memory.h"
#include "runtime/darwin_boot_milestones.h"
#include "runtime/darwin_platform_expert.h"

namespace winios::runtime {

// -----------------------------------------------------------------------------
// DarwinBootConfig: Target profile and boot parameters for Darwin / iOS
// Configured by default for iPhone 15 (iPhone15,4 / Apple A16 Bionic T8120)
// -----------------------------------------------------------------------------
struct DarwinBootConfig {
    std::string kernelcache_path;
    std::string boot_args{"-v debug=0x144 serial=3 keepsyms=1"};
    std::uint64_t ram_base{0x40000000ULL};
    std::size_t ram_size{0x10000000}; // 256 MB minimum VM RAM, up to 6 GB
    std::size_t core_count{6};         // 2 Everest Performance + 4 Sawtooth Efficiency
    std::size_t max_instructions{10000000};
    std::string console_log_path;

    // Peripheral geometries
    std::uint64_t aic_base{device::AppleAIC::kDefaultAicBase};
    std::uint64_t pmgr_base{device::ApplePMGR::kDefaultPmgrBase};
    std::uint64_t uart_base{device::VirtualUartDevice::kDefaultUartBase};
    std::uint64_t display_base{device::AppleDisplay::kDefaultDisplayBase};
    std::uint64_t framebuffer_base{0x4F000000ULL}; // Framebuffer within RAM window

    // Optional direct memory buffer (for testing and in-memory execution)
    std::vector<std::uint8_t> kernelcache_data;

    // Optional ramdisk root filesystem (APFS container / DMG)
    std::string ramdisk_path;
    std::vector<std::uint8_t> ramdisk_data;
};

// -----------------------------------------------------------------------------
// DarwinSessionStatus: Operational lifecycle state of the Darwin boot VM
// -----------------------------------------------------------------------------
enum class DarwinSessionStatus {
    Uninitialized,
    Prepared,
    BootHandoffReady,
    Running,
    Halted,
    MaxInstructionsReached,
    Error
};

// -----------------------------------------------------------------------------
// DarwinBootSession: Unified orchestrator for booting native Darwin/iOS kernel
// Coordinates Apple AIC 2, PMGR, Framebuffer Display, Apple DeviceTree (ADTS),
// Apple struct boot_args, 64-bit Mach-O loader, and AArch64 CPU handoff.
// -----------------------------------------------------------------------------
class DarwinBootSession {
public:
    explicit DarwinBootSession(const DarwinBootConfig& config);
    ~DarwinBootSession() = default;

    DarwinBootSession(const DarwinBootSession&) = delete;
    DarwinBootSession& operator=(const DarwinBootSession&) = delete;

    // VM Lifecycle
    bool initialize(std::string& out_error);
    bool prepare_handoff(std::string& out_error);

    // Stepping & Execution
    std::size_t step(std::size_t max_instructions = 1);
    DarwinSessionStatus run();

    // Inspection & Queries
    [[nodiscard]] DarwinSessionStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_; }
    [[nodiscard]] std::string get_console_output() const;
    [[nodiscard]] std::size_t total_instructions_executed() const noexcept { return total_instructions_; }

    [[nodiscard]] const DarwinBootConfig& config() const noexcept { return config_; }
    [[nodiscard]] const loader::GuestMemoryMap& memory_map() const noexcept { return memory_map_; }
    [[nodiscard]] const loader::MachOLoadResult& kernel_result() const noexcept { return kernel_result_; }
    [[nodiscard]] const firmware::AppleBootArgs& boot_args() const noexcept { return boot_args_; }

    [[nodiscard]] std::uint64_t boot_args_phys_address() const noexcept { return boot_args_phys_addr_; }
    [[nodiscard]] std::uint64_t devicetree_phys_address() const noexcept { return devicetree_phys_addr_; }
    [[nodiscard]] std::size_t   devicetree_length() const noexcept { return devicetree_length_; }

    // Subsystem accessors
    [[nodiscard]] device::AppleAIC& aic() noexcept { return *aic_; }
    [[nodiscard]] const device::AppleAIC& aic() const noexcept { return *aic_; }
    [[nodiscard]] device::ApplePMGR& pmgr() noexcept { return *pmgr_; }
    [[nodiscard]] const device::ApplePMGR& pmgr() const noexcept { return *pmgr_; }
    [[nodiscard]] device::AppleDisplay& display() noexcept { return *display_; }
    [[nodiscard]] const device::AppleDisplay& display() const noexcept { return *display_; }
    [[nodiscard]] device::VirtualUartDevice& uart() noexcept { return *uart_; }
    [[nodiscard]] const device::VirtualUartDevice& uart() const noexcept { return *uart_; }
    [[nodiscard]] CPU& boot_cpu() noexcept { return *cpus_[0]; }
    [[nodiscard]] const CPU& boot_cpu() const noexcept { return *cpus_[0]; }
    [[nodiscard]] Memory& memory() noexcept { return *memory_; }
    [[nodiscard]] const Memory& memory() const noexcept { return *memory_; }

    [[nodiscard]] const DarwinPlatformExpert& platform_expert() const noexcept { return platform_expert_; }
    [[nodiscard]] DarwinPlatformExpert& platform_expert() noexcept { return platform_expert_; }
    [[nodiscard]] const DarwinBootMilestoneTracker& milestone_tracker() const noexcept { return milestone_tracker_; }
    [[nodiscard]] DarwinBootMilestoneTracker& milestone_tracker() noexcept { return milestone_tracker_; }
    [[nodiscard]] const loader::RamdiskLoadResult& ramdisk_result() const noexcept { return ramdisk_result_; }

    void set_status(DarwinSessionStatus s) noexcept { status_ = s; }
    void set_total_instructions(std::size_t n) noexcept { total_instructions_ = n; }

private:
    DarwinBootConfig config_;
    DarwinSessionStatus status_{DarwinSessionStatus::Uninitialized};
    std::string error_;

    std::unique_ptr<Memory> memory_;
    std::vector<std::unique_ptr<CPU>> cpus_;
    std::shared_ptr<device::VirtualDeviceBus> device_bus_;
    std::shared_ptr<device::AppleAIC> aic_;
    std::shared_ptr<device::ApplePMGR> pmgr_;
    std::shared_ptr<device::AppleDisplay> display_;
    std::shared_ptr<device::VirtualUartDevice> uart_;

    loader::GuestMemoryMap memory_map_;
    loader::MachOLoadResult kernel_result_{};
    loader::RamdiskLoadResult ramdisk_result_{};

    DarwinPlatformExpert platform_expert_;
    DarwinBootMilestoneTracker milestone_tracker_;

    firmware::AppleBootArgs boot_args_{};
    std::uint64_t boot_args_phys_addr_{0};
    std::uint64_t devicetree_phys_addr_{0};
    std::size_t   devicetree_length_{0};
    std::size_t   total_instructions_{0};
};

} // namespace winios::runtime
