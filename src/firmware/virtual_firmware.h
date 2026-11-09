#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "device/virtual_platform.h"

namespace winios::firmware {

// -----------------------------------------------------------------------------
// BootLifecycleState: Explicit virtual machine boot lifecycle states
// -----------------------------------------------------------------------------
enum class BootLifecycleState : std::uint8_t {
    Uninitialized       = 0,
    PlatformInitialized = 1,
    FirmwareInitialized = 2,
    GuestPrepared       = 3,
    GuestHandoff        = 4,
    Running             = 5,
    Halted              = 6,
    Shutdown            = 7,
    Error               = 8
};

[[nodiscard]] const char* boot_lifecycle_state_to_string(BootLifecycleState state) noexcept;

// -----------------------------------------------------------------------------
// FirmwareConfig: Configuration parameters for firmware boot lifecycle
// -----------------------------------------------------------------------------
struct FirmwareConfig {
    std::uint64_t ram_base{0x40000000ULL};
    std::uint64_t ram_size{0x08000000ULL};     // 128 MB default
    std::uint64_t kernel_entry{0x40080000ULL}; // Standard ARM64 load offset (base + 0x80000)
    std::uint64_t dtb_address{0x47000000ULL};  // Placed in upper RAM
    std::uint64_t initrd_address{0x0ULL};
    std::uint64_t initrd_size{0x0ULL};
    std::string boot_args{"console=ttyAMA0 earlycon=pl011,0x09000000"};
    std::uint32_t target_el{1};                // EL1 by default
    bool enable_smp{true};
    std::size_t core_count{1};
};

// -----------------------------------------------------------------------------
// VirtualFirmware: Firmware and Boot Coordinator
// -----------------------------------------------------------------------------
class VirtualFirmware {
public:
    explicit VirtualFirmware(FirmwareConfig config = {});
    ~VirtualFirmware() = default;

    VirtualFirmware(const VirtualFirmware&) = delete;
    VirtualFirmware& operator=(const VirtualFirmware&) = delete;

    // State Inspection
    [[nodiscard]] BootLifecycleState state() const noexcept { return state_; }
    [[nodiscard]] const FirmwareConfig& config() const noexcept { return config_; }
    [[nodiscard]] const std::string& last_error() const noexcept { return last_error_; }
    [[nodiscard]] bool has_error() const noexcept { return state_ == BootLifecycleState::Error; }

    // Configuration
    void set_config(const FirmwareConfig& config);

    // Lifecycle State Machine Transitions
    bool initialize_platform(device::VirtualPlatform& platform);
    bool initialize_firmware();
    bool prepare_guest(std::uint64_t kernel_entry, std::uint64_t dtb_address,
                       std::uint64_t initrd_address = 0, std::uint64_t initrd_size = 0);
    bool handoff_to_guest();
    bool start_execution();
    bool halt_guest();
    bool shutdown();
    void reset();

    // Verification helpers
    [[nodiscard]] bool verify_initial_cpu_state(std::size_t core_id) const;

private:
    void transition_to_error(std::string message);
    [[nodiscard]] bool is_valid_ram_address(std::uint64_t address, std::size_t size = 1) const noexcept;

    FirmwareConfig config_;
    BootLifecycleState state_{BootLifecycleState::Uninitialized};
    std::string last_error_;
    device::VirtualPlatform* bound_platform_{nullptr};
};

} // namespace winios::firmware
