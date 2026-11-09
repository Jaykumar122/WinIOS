#include "firmware/apple_boot_config_manager.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace winios::firmware {

std::string AppleBootConfig::build_command_line() const {
    std::stringstream ss;
    bool first = true;

    auto append_arg = [&](const std::string& arg) {
        if (!arg.empty()) {
            if (!first) ss << " ";
            ss << arg;
            first = false;
        }
    };

    if (verbose_boot) append_arg("-v");

    if (debug_flags != 0) {
        std::stringstream dbg_ss;
        dbg_ss << "debug=0x" << std::hex << debug_flags;
        append_arg(dbg_ss.str());
    }

    if (serial_flags != 0) {
        append_arg("serial=" + std::to_string(serial_flags));
    }

    if (keep_symbols) append_arg("keepsyms=1");
    if (disable_watchdog) append_arg("wdt=0");
    if (disable_cs_enforcement) append_arg("cs_enforcement_disable=1");

    if (!custom_boot_args.empty()) {
        append_arg(custom_boot_args);
    }

    return ss.str();
}

AppleBootConfigManager::AppleBootConfigManager()
    : config_(create_iphone15_default()) {}

AppleBootConfigManager::AppleBootConfigManager(AppleBootConfig config)
    : config_(std::move(config)) {}

AppleBootConfig AppleBootConfigManager::create_iphone15_default() {
    AppleBootConfig cfg;
    cfg.device_model = "iPhone15,4";
    cfg.board_id_str = "D37AP";
    cfg.board_id = 0x18;
    cfg.chip_id = 0x8120;
    cfg.ram_base = 0x40000000ULL;
    cfg.ram_size = 6ULL * 1024 * 1024 * 1024; // 6 GB LPDDR5
    cfg.cpu_core_count = 6;
    cfg.active_core_mask = 0x3F;
    cfg.timebase_frequency_hz = 24000000;
    cfg.smp_enabled = true;
    cfg.verbose_boot = true;
    cfg.debug_flags = 0x144;
    cfg.serial_flags = 3;
    cfg.display_width = 1179;
    cfg.display_height = 2556;
    cfg.display_depth = 32;
    cfg.display_row_bytes = 1179 * 4;
    cfg.refresh_rate_hz = 60;
    cfg.root_mount = RootMountType::RamdiskRoot;
    return cfg;
}

AppleBootConfig AppleBootConfigManager::create_iphone14_pro_default() {
    AppleBootConfig cfg = create_iphone15_default();
    cfg.device_model = "iPhone15,2";
    cfg.board_id_str = "D73AP";
    return cfg;
}

AppleBootConfig AppleBootConfigManager::create_iphone15_verbose_debug() {
    AppleBootConfig cfg = create_iphone15_default();
    cfg.verbose_boot = true;
    cfg.debug_flags = 0x14E; // DB_KPRT | DB_PRT | DB_LOG_PI_SCRN | DB_SLOG
    cfg.serial_flags = 3;
    cfg.keep_symbols = true;
    cfg.disable_watchdog = true;
    cfg.disable_cs_enforcement = true;
    cfg.custom_boot_args = "msgbuf=1048576 pgoff=0";
    return cfg;
}

AppleBootConfig AppleBootConfigManager::create_iphone15_ramdisk_recovery() {
    AppleBootConfig cfg = create_iphone15_default();
    cfg.root_mount = RootMountType::RamdiskRoot;
    cfg.ramdisk_phys = 0x4A000000ULL;
    cfg.ramdisk_size = 128ULL * 1024 * 1024; // 128 MB default ramdisk
    cfg.custom_boot_args = "rd=md0";
    return cfg;
}

AppleBootConfig AppleBootConfigManager::create_iphone15_apfs_root() {
    AppleBootConfig cfg = create_iphone15_default();
    cfg.root_mount = RootMountType::ApfsSystemVolume;
    cfg.root_device_path = "/dev/disk0s1s1";
    cfg.read_only_root = true;
    cfg.custom_boot_args = "rootdev=disk0s1s1";
    return cfg;
}

bool AppleBootConfigManager::validate(std::string& out_error) const {
    if (config_.ram_base % 16384ULL != 0) {
        out_error = "RAM base address (0x" + std::to_string(config_.ram_base) + ") must be 16 KB page-aligned";
        return false;
    }

    if (config_.ram_size < 256ULL * 1024 * 1024) {
        out_error = "RAM size (" + std::to_string(config_.ram_size / (1024 * 1024)) + " MB) is below 256 MB minimum";
        return false;
    }

    if (config_.ram_size % 16384ULL != 0) {
        out_error = "RAM size must be a multiple of 16 KB page granule";
        return false;
    }

    if (config_.cpu_core_count == 0 || config_.cpu_core_count > 6) {
        out_error = "iPhone 15 CPU core count must be between 1 and 6 (configured: " + std::to_string(config_.cpu_core_count) + ")";
        return false;
    }

    if (config_.active_core_mask == 0 || (config_.active_core_mask & ~0x3Fu) != 0) {
        out_error = "Invalid active core mask for 6-core SoC";
        return false;
    }

    if (config_.display_width == 0 || config_.display_height == 0 || config_.display_depth != 32) {
        out_error = "Display geometry invalid (expected 32-bit ARGB with non-zero dimensions)";
        return false;
    }

    if (config_.display_row_bytes != config_.display_width * 4) {
        out_error = "Display row_bytes must equal width * 4 for 32-bit ARGB";
        return false;
    }

    if (config_.timebase_frequency_hz == 0) {
        out_error = "Timebase frequency cannot be zero";
        return false;
    }

    return true;
}

bool AppleBootConfigManager::validate_against_hardware_profile(
    const device::AppleHardwareProfile& profile,
    std::string& out_error) const {
    if (!validate(out_error)) {
        return false;
    }

    if (config_.board_id != profile.board_id) {
        out_error = "Configured board-id (0x" + std::to_string(config_.board_id) +
                    ") does not match hardware profile (0x" + std::to_string(profile.board_id) + ")";
        return false;
    }

    if (config_.chip_id != profile.chip_id) {
        out_error = "Configured chip-id (0x" + std::to_string(config_.chip_id) +
                    ") does not match hardware profile (0x" + std::to_string(profile.chip_id) + ")";
        return false;
    }

    if (config_.timebase_frequency_hz != profile.timebase_frequency_hz) {
        out_error = "Timebase frequency (" + std::to_string(config_.timebase_frequency_hz) +
                    " Hz) does not match hardware specification (" +
                    std::to_string(profile.timebase_frequency_hz) + " Hz)";
        return false;
    }

    if (config_.display_width != profile.display.width || config_.display_height != profile.display.height) {
        out_error = "Display dimensions do not match iPhone 15 Super Retina XDR profile (1179x2556)";
        return false;
    }

    return true;
}

AppleBootArgs AppleBootConfigManager::export_darwin_boot_args(
    std::uint64_t virt_kernel_base,
    std::uint64_t top_of_kernel,
    std::uint32_t devicetree_length) const {
    AppleBootVideo video;
    video.v_baseAddr = config_.framebuffer_phys;
    video.v_display = 1;
    video.v_rowBytes = config_.display_row_bytes;
    video.v_width = config_.display_width;
    video.v_height = config_.display_height;
    video.v_depth = config_.display_depth;

    std::string cmdline = config_.build_command_line();

    return AppleBootArgsBuilder::create(
        config_.ram_base,
        virt_kernel_base,
        config_.ram_size,
        top_of_kernel,
        config_.devicetree_phys,
        devicetree_length,
        cmdline,
        video);
}

std::string AppleBootConfigManager::serialize_to_key_value() const {
    std::stringstream ss;
    ss << "config_version=" << config_.config_version << "\n";
    ss << "device_model=" << config_.device_model << "\n";
    ss << "board_id=0x" << std::hex << config_.board_id << std::dec << "\n";
    ss << "chip_id=0x" << std::hex << config_.chip_id << std::dec << "\n";
    ss << "ram_base=0x" << std::hex << config_.ram_base << std::dec << "\n";
    ss << "ram_size=" << config_.ram_size << "\n";
    ss << "kernel_load_phys=0x" << std::hex << config_.kernel_load_phys << std::dec << "\n";
    ss << "devicetree_phys=0x" << std::hex << config_.devicetree_phys << std::dec << "\n";
    ss << "ramdisk_phys=0x" << std::hex << config_.ramdisk_phys << std::dec << "\n";
    ss << "ramdisk_size=" << config_.ramdisk_size << "\n";
    ss << "framebuffer_phys=0x" << std::hex << config_.framebuffer_phys << std::dec << "\n";
    ss << "cpu_core_count=" << config_.cpu_core_count << "\n";
    ss << "active_core_mask=0x" << std::hex << config_.active_core_mask << std::dec << "\n";
    ss << "timebase_frequency_hz=" << config_.timebase_frequency_hz << "\n";
    ss << "verbose_boot=" << (config_.verbose_boot ? "1" : "0") << "\n";
    ss << "debug_flags=0x" << std::hex << config_.debug_flags << std::dec << "\n";
    ss << "serial_flags=" << config_.serial_flags << "\n";
    ss << "keep_symbols=" << (config_.keep_symbols ? "1" : "0") << "\n";
    ss << "disable_watchdog=" << (config_.disable_watchdog ? "1" : "0") << "\n";
    ss << "disable_cs_enforcement=" << (config_.disable_cs_enforcement ? "1" : "0") << "\n";
    ss << "custom_boot_args=" << config_.custom_boot_args << "\n";
    ss << "display_width=" << config_.display_width << "\n";
    ss << "display_height=" << config_.display_height << "\n";
    ss << "display_depth=" << config_.display_depth << "\n";
    ss << "root_mount=" << (config_.root_mount == RootMountType::RamdiskRoot ? "ramdisk" : "apfs") << "\n";
    ss << "root_device_path=" << config_.root_device_path << "\n";
    return ss.str();
}

bool AppleBootConfigManager::deserialize_from_key_value(std::string_view kv_string, std::string& out_error) {
    std::string kv_copy(kv_string);
    std::stringstream ss(kv_copy);
    std::string line;

    while (std::getline(ss, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto parse_u64 = [](const std::string& str) -> std::uint64_t {
            if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) {
                return std::stoull(str, nullptr, 16);
            }
            return std::stoull(str, nullptr, 10);
        };

        try {
            if (key == "config_version") config_.config_version = val;
            else if (key == "device_model") config_.device_model = val;
            else if (key == "board_id") config_.board_id = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "chip_id") config_.chip_id = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "ram_base") config_.ram_base = parse_u64(val);
            else if (key == "ram_size") config_.ram_size = parse_u64(val);
            else if (key == "kernel_load_phys") config_.kernel_load_phys = parse_u64(val);
            else if (key == "devicetree_phys") config_.devicetree_phys = parse_u64(val);
            else if (key == "ramdisk_phys") config_.ramdisk_phys = parse_u64(val);
            else if (key == "ramdisk_size") config_.ramdisk_size = parse_u64(val);
            else if (key == "framebuffer_phys") config_.framebuffer_phys = parse_u64(val);
            else if (key == "cpu_core_count") config_.cpu_core_count = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "active_core_mask") config_.active_core_mask = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "timebase_frequency_hz") config_.timebase_frequency_hz = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "verbose_boot") config_.verbose_boot = (val == "1" || val == "true");
            else if (key == "debug_flags") config_.debug_flags = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "serial_flags") config_.serial_flags = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "keep_symbols") config_.keep_symbols = (val == "1" || val == "true");
            else if (key == "disable_watchdog") config_.disable_watchdog = (val == "1" || val == "true");
            else if (key == "disable_cs_enforcement") config_.disable_cs_enforcement = (val == "1" || val == "true");
            else if (key == "custom_boot_args") config_.custom_boot_args = val;
            else if (key == "display_width") config_.display_width = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "display_height") config_.display_height = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "display_depth") config_.display_depth = static_cast<std::uint32_t>(parse_u64(val));
            else if (key == "root_mount") config_.root_mount = (val == "apfs" ? RootMountType::ApfsSystemVolume : RootMountType::RamdiskRoot);
            else if (key == "root_device_path") config_.root_device_path = val;
        } catch (const std::exception& e) {
            out_error = "Failed to parse key '" + key + "' value '" + val + "': " + e.what();
            return false;
        }
    }

    return validate(out_error);
}

std::string AppleBootConfigManager::generate_config_report() const {
    std::stringstream ss;
    ss << "# iPhone 15 Genuine Boot Configuration Report\n\n";
    ss << "| Configuration Parameter | Value | Hardware / Architectural Constraint |\n";
    ss << "| :--- | :--- | :--- |\n";
    ss << "| **Target Device** | `" << config_.device_model << " (" << config_.board_id_str << ")` | Board `0x18`, Chip `0x8120` |\n";
    ss << "| **RAM Topology** | `Base 0x" << std::hex << config_.ram_base << ", Size " << std::dec
       << (config_.ram_size / (1024 * 1024)) << " MB` | 16 KB Granule Aligned (LPDDR5) |\n";
    ss << "| **CPU / SMP Topology** | `" << config_.cpu_core_count << " Cores, Mask 0x"
       << std::hex << config_.active_core_mask << std::dec << "` | 2 Everest @ 3.46 GHz + 4 Sawtooth @ 2.02 GHz |\n";
    ss << "| **Timebase Frequency** | `" << config_.timebase_frequency_hz << " Hz` | Apple A16 Bionic 24 MHz Clock |\n";
    ss << "| **Kernel Command Line** | `" << config_.build_command_line() << "` | Passed via `AppleBootArgs.CommandLine` |\n";
    ss << "| **Display Geometry** | `" << config_.display_width << "x" << config_.display_height << " @ "
       << config_.display_depth << "-bit` | Super Retina XDR Panel |\n";
    ss << "| **Root Filesystem** | `" << (config_.root_mount == RootMountType::RamdiskRoot ? "RamdiskRoot" : "APFS SSV")
       << " (" << config_.root_device_path << ")` | XNU VFS Root Target |\n";
    return ss.str();
}

} // namespace winios::firmware
