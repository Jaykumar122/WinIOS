#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "device/fdt_builder.h"
#include "device/virtual_platform.h"
#include "firmware/virtual_firmware.h"
#include "loader/guest_memory_map.h"
#include "loader/initramfs_loader.h"
#include "loader/linux_image_loader.h"
#include "runtime/linux_boot_milestones.h"

namespace winios::runtime {

struct LinuxBootConfig {
    std::string kernel_path;
    std::string dtb_path;
    std::string initramfs_path;
    std::string boot_args{"console=ttyAMA0 earlycon=pl011,0x09000000 root=/dev/ram0 rw"};
    std::uint64_t ram_base{0x40000000ULL};
    std::size_t ram_size{0x08000000}; // 128 MB default
    std::size_t core_count{1};
    std::size_t max_instructions{10000000};
    std::string console_log_path;

    // Optional direct memory buffers (useful for embedded testing)
    std::vector<std::uint8_t> kernel_data;
    std::vector<std::uint8_t> dtb_data;
    std::vector<std::uint8_t> initramfs_data;
};

enum class LinuxSessionStatus {
    Uninitialized,
    Prepared,
    Running,
    Halted,
    MaxInstructionsReached,
    Error
};

class LinuxBootSession {
public:
    explicit LinuxBootSession(const LinuxBootConfig& config);
    ~LinuxBootSession() = default;

    LinuxBootSession(const LinuxBootSession&) = delete;
    LinuxBootSession& operator=(const LinuxBootSession&) = delete;

    // Lifecycle
    bool initialize(std::string& out_error);
    bool boot(std::string& out_error);

    // Stepping & Execution
    std::size_t step(std::size_t max_instructions = 1);
    LinuxSessionStatus run();

    // Inspection & Queries
    [[nodiscard]] LinuxSessionStatus status() const noexcept { return status_; }
    [[nodiscard]] const std::string& error_message() const noexcept { return error_; }
    [[nodiscard]] std::string get_console_output() const;
    [[nodiscard]] std::size_t total_instructions_executed() const noexcept { return total_instructions_; }

    [[nodiscard]] const LinuxBootConfig& config() const noexcept { return config_; }
    [[nodiscard]] const device::VirtualPlatform& platform() const noexcept { return *platform_; }
    [[nodiscard]] device::VirtualPlatform& platform() noexcept { return *platform_; }
    [[nodiscard]] const firmware::VirtualFirmware& firmware() const noexcept { return *firmware_; }
    [[nodiscard]] const loader::GuestMemoryMap& memory_map() const noexcept { return memory_map_; }
    [[nodiscard]] const loader::LinuxKernelLoadResult& kernel_result() const noexcept { return kernel_result_; }
    [[nodiscard]] const loader::InitramfsLoadResult& initramfs_result() const noexcept { return initramfs_result_; }
    [[nodiscard]] LinuxBootMilestoneTracker& milestone_tracker() noexcept { return milestone_tracker_; }
    [[nodiscard]] const LinuxBootMilestoneTracker& milestone_tracker() const noexcept { return milestone_tracker_; }

    void set_status(LinuxSessionStatus status) noexcept { status_ = status; }
    void set_total_instructions(std::size_t count) noexcept { total_instructions_ = count; }

private:
    LinuxBootConfig config_;
    LinuxSessionStatus status_{LinuxSessionStatus::Uninitialized};
    std::string error_;

    std::unique_ptr<device::VirtualPlatform> platform_;
    std::unique_ptr<firmware::VirtualFirmware> firmware_;
    loader::GuestMemoryMap memory_map_;
    LinuxBootMilestoneTracker milestone_tracker_;

    loader::LinuxKernelLoadResult kernel_result_{};
    loader::InitramfsLoadResult initramfs_result_{};

    std::uint64_t dtb_address_{0};
    std::size_t dtb_size_{0};
    std::size_t total_instructions_{0};
};

} // namespace winios::runtime
