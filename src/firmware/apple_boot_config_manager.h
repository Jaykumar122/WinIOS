#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "device/apple_hardware_profile.h"
#include "firmware/apple_boot_args.h"

namespace winios::firmware {

// -----------------------------------------------------------------------------
// SepEmulationMode: Secure Enclave Processor emulation strategy
// -----------------------------------------------------------------------------
enum class SepEmulationMode {
    Stub,           // Host responds to SEP mailbox commands with success stubs
    Passthrough,    // SEP messages routed to secondary thread / external emulator
    HardwareDirect  // Real hardware SEP (blocked without physical crypto keys)
};

// -----------------------------------------------------------------------------
// RootMountType: Root filesystem selection for XNU VFS
// -----------------------------------------------------------------------------
enum class RootMountType {
    RamdiskRoot,        // Boot from early memory ramdisk (Restore / fsck)
    ApfsSystemVolume,   // Boot from genuine APFS Sealed System Volume (SSV)
    HostVfsShare        // Direct pass-through to host folder
};

// -----------------------------------------------------------------------------
// ConsoleSinkType: Darwin kernel print / serial output destination
// -----------------------------------------------------------------------------
enum class ConsoleSinkType {
    Stdout,
    File,
    RingBuffer,
    Null
};

// -----------------------------------------------------------------------------
// AppleBootConfig: Comprehensive, versioned boot configuration for iPhone 15
// -----------------------------------------------------------------------------
struct AppleBootConfig {
    std::string config_version{"1.0"};
    std::string device_model{"iPhone15,4"};
    std::string board_id_str{"D37AP"};
    std::uint32_t board_id{0x18};
    std::uint32_t chip_id{0x8120};

    // Physical Memory Layout
    std::uint64_t ram_base{0x40000000ULL};                     // 1 GB physical RAM base
    std::uint64_t ram_size{6ULL * 1024 * 1024 * 1024};         // 6 GB standard LPDDR5
    std::uint64_t kernel_load_phys{0x40000000ULL};             // Staging address for Mach-O
    std::uint64_t devicetree_phys{0x48000000ULL};              // Staging address for ADTS blob
    std::uint64_t ramdisk_phys{0x4A000000ULL};                 // Staging address for RAMDisk
    std::uint64_t ramdisk_size{0};                             // Size of loaded ramdisk
    std::uint64_t framebuffer_phys{0x4F000000ULL};             // Display framebuffer

    // CPU and SMP Topology
    std::uint32_t cpu_core_count{6};                           // 2 Everest + 4 Sawtooth
    std::uint32_t active_core_mask{0x3F};                      // 6 cores active (0b111111)
    std::uint32_t timebase_frequency_hz{24000000};             // 24 MHz
    bool smp_enabled{true};

    // Command Line Flags
    bool verbose_boot{true};                                   // -v
    bool keep_symbols{true};                                   // keepsyms=1
    bool disable_watchdog{true};                               // wdt=0
    bool disable_cs_enforcement{true};                         // cs_enforcement_disable=1
    std::uint32_t debug_flags{0x144};                          // debug=0x144 (DB_KPRT | DB_PRT | DB_LOG_PI_SCRN)
    std::uint32_t serial_flags{3};                             // serial=3 (cons_ops=3)
    std::string custom_boot_args;                              // Additional flags

    // Subsystems & Security
    SepEmulationMode sep_mode{SepEmulationMode::Stub};
    bool pac_enabled{true};                                    // ARM64e Pointer Authentication
    bool strict_trust_cache{false};                            // Relaxed for developer/synthetic
    RootMountType root_mount{RootMountType::RamdiskRoot};
    std::string root_device_path{"/dev/disk0s1s1"};
    bool read_only_root{true};

    // Display & UI
    std::uint32_t display_width{1179};
    std::uint32_t display_height{2556};
    std::uint32_t display_depth{32};
    std::uint32_t display_row_bytes{1179 * 4};
    std::uint32_t refresh_rate_hz{60};
    bool enable_display_window{true};

    // Logging & Console
    ConsoleSinkType console_sink{ConsoleSinkType::Stdout};
    std::string log_file_path{"darwin_boot.log"};

    // Helpers
    [[nodiscard]] std::string build_command_line() const;
};

// -----------------------------------------------------------------------------
// AppleBootConfigManager: Manages, validates, and serializes boot configurations
// -----------------------------------------------------------------------------
class AppleBootConfigManager {
public:
    AppleBootConfigManager();
    explicit AppleBootConfigManager(AppleBootConfig config);
    ~AppleBootConfigManager() = default;

    // Config accessors
    [[nodiscard]] const AppleBootConfig& config() const noexcept { return config_; }
    AppleBootConfig& config() noexcept { return config_; }
    void set_config(AppleBootConfig config) noexcept { config_ = std::move(config); }

    // Preset factories
    [[nodiscard]] static AppleBootConfig create_iphone15_default();
    [[nodiscard]] static AppleBootConfig create_iphone14_pro_default();
    [[nodiscard]] static AppleBootConfig create_iphone15_verbose_debug();
    [[nodiscard]] static AppleBootConfig create_iphone15_ramdisk_recovery();
    [[nodiscard]] static AppleBootConfig create_iphone15_apfs_root();

    // Validation against hardware constraints
    [[nodiscard]] bool validate(std::string& out_error) const;
    [[nodiscard]] bool validate_against_hardware_profile(
        const device::AppleHardwareProfile& profile,
        std::string& out_error) const;

    // Conversion to Darwin XNU AppleBootArgs structure
    [[nodiscard]] AppleBootArgs export_darwin_boot_args(
        std::uint64_t virt_kernel_base = 0xFFFFFE0007004000ULL,
        std::uint64_t top_of_kernel = 0x42000000ULL,
        std::uint32_t devicetree_length = 0) const;

    // Structured Key-Value serialization and deserialization
    [[nodiscard]] std::string serialize_to_key_value() const;
    [[nodiscard]] bool deserialize_from_key_value(std::string_view kv_string, std::string& out_error);

    // Formatted inspection markdown report
    [[nodiscard]] std::string generate_config_report() const;

private:
    AppleBootConfig config_;
};

} // namespace winios::firmware
