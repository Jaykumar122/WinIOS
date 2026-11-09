#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

#include "firmware/apple_boot_config_manager.h"
#include "device/apple_hardware_profile.h"

using namespace winios::firmware;
using namespace winios::device;

#define STAGE97_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[STAGE 97 ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Default iPhone 15 Configuration
// -----------------------------------------------------------------------------
static bool test_default_iphone15_configuration() {
    std::cout << "\n[1] test_default_iphone15_configuration...\n";

    AppleBootConfigManager mgr;
    const auto& cfg = mgr.config();

    STAGE97_ASSERT(cfg.device_model == "iPhone15,4", "Device model must be iPhone15,4");
    STAGE97_ASSERT(cfg.board_id == 0x18, "Board ID must be 0x18 (D73AP)");
    STAGE97_ASSERT(cfg.chip_id == 0x8120, "Chip ID must be 0x8120 (T8120)");
    STAGE97_ASSERT(cfg.cpu_core_count == 6, "CPU core count must be 6 (2 Everest + 4 Sawtooth)");
    STAGE97_ASSERT(cfg.active_core_mask == 0x3F, "Active core mask must be 0x3F (all 6 cores)");
    STAGE97_ASSERT(cfg.timebase_frequency_hz == 24000000, "Timebase frequency must be 24 MHz");
    STAGE97_ASSERT(cfg.ram_size == 6ULL * 1024 * 1024 * 1024, "Default RAM size must be 6 GB");
    STAGE97_ASSERT(cfg.ram_base % 16384 == 0, "RAM base must be 16 KB aligned");
    STAGE97_ASSERT(cfg.display_width == 1179 && cfg.display_height == 2556, "Super Retina XDR geometry 1179x2556");
    STAGE97_ASSERT(cfg.display_row_bytes == 1179 * 4, "Row bytes must be 4716");

    std::string err;
    STAGE97_ASSERT(mgr.validate(err), "Default configuration must pass validation");

    std::cout << "[PASS] Default iPhone 15 Configuration Verified\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Command Line Composition and Flag Management
// -----------------------------------------------------------------------------
static bool test_command_line_composition() {
    std::cout << "\n[2] test_command_line_composition...\n";

    AppleBootConfig cfg = AppleBootConfigManager::create_iphone15_verbose_debug();
    std::string cmdline = cfg.build_command_line();

    STAGE97_ASSERT(cmdline.find("-v") != std::string::npos, "Must include -v");
    STAGE97_ASSERT(cmdline.find("debug=0x14e") != std::string::npos || cmdline.find("debug=0x14E") != std::string::npos,
                   "Must include debug flags");
    STAGE97_ASSERT(cmdline.find("serial=3") != std::string::npos, "Must include serial=3");
    STAGE97_ASSERT(cmdline.find("keepsyms=1") != std::string::npos, "Must include keepsyms=1");
    STAGE97_ASSERT(cmdline.find("wdt=0") != std::string::npos, "Must include wdt=0");
    STAGE97_ASSERT(cmdline.find("cs_enforcement_disable=1") != std::string::npos, "Must include cs_enforcement_disable=1");
    STAGE97_ASSERT(cmdline.find("msgbuf=1048576") != std::string::npos, "Must include custom boot args");

    std::cout << "Constructed Command Line: \"" << cmdline << "\"\n";
    std::cout << "[PASS] Command Line Composition and Flag Management\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Preset Profile Matrix
// -----------------------------------------------------------------------------
static bool test_preset_profiles() {
    std::cout << "\n[3] test_preset_profiles...\n";

    auto def = AppleBootConfigManager::create_iphone15_default();
    auto dbg = AppleBootConfigManager::create_iphone15_verbose_debug();
    auto rec = AppleBootConfigManager::create_iphone15_ramdisk_recovery();
    auto apfs = AppleBootConfigManager::create_iphone15_apfs_root();

    STAGE97_ASSERT(rec.root_mount == RootMountType::RamdiskRoot, "Recovery profile must use RamdiskRoot");
    STAGE97_ASSERT(rec.ramdisk_phys != 0 && rec.ramdisk_size > 0, "Recovery profile must have non-zero ramdisk");

    STAGE97_ASSERT(apfs.root_mount == RootMountType::ApfsSystemVolume, "APFS profile must use ApfsSystemVolume");
    STAGE97_ASSERT(apfs.root_device_path == "/dev/disk0s1s1", "APFS root device path must be /dev/disk0s1s1");

    STAGE97_ASSERT(dbg.keep_symbols && dbg.disable_watchdog, "Debug profile must enable symbols and disable wdt");

    std::cout << "[PASS] Preset Profile Matrix (Default, Verbose, Ramdisk, APFS)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Hardware Profile Validation & Constraint Enforcement
// -----------------------------------------------------------------------------
static bool test_hardware_profile_validation() {
    std::cout << "\n[4] test_hardware_profile_validation...\n";

    AppleBootConfigManager mgr;
    auto hw_profile = AppleHardwareProfile::get_iphone15_profile();

    std::string err;
    STAGE97_ASSERT(mgr.validate_against_hardware_profile(hw_profile, err),
                   "Default config must pass hardware profile validation");

    // 1. Deliberate board-id mismatch
    mgr.config().board_id = 0x20;
    STAGE97_ASSERT(!mgr.validate_against_hardware_profile(hw_profile, err),
                   "Mismatched board-id must be rejected");
    STAGE97_ASSERT(err.find("board-id") != std::string::npos, "Error must mention board-id");
    mgr.config().board_id = 0x18; // Restore

    // 2. Deliberate misaligned RAM base (not 16 KB aligned)
    mgr.config().ram_base = 0x40001000ULL; // +4 KB
    STAGE97_ASSERT(!mgr.validate_against_hardware_profile(hw_profile, err),
                   "Misaligned RAM base must be rejected");
    mgr.config().ram_base = 0x40000000ULL; // Restore

    // 3. Deliberate invalid core count (> 6)
    mgr.config().cpu_core_count = 8;
    STAGE97_ASSERT(!mgr.validate(err), "8 cores on iPhone 15 must be rejected");
    mgr.config().cpu_core_count = 6; // Restore

    std::cout << "[PASS] Hardware Profile Validation & Constraint Enforcement\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Darwin XNU AppleBootArgs Structure Export
// -----------------------------------------------------------------------------
static bool test_darwin_boot_args_export() {
    std::cout << "\n[5] test_darwin_boot_args_export...\n";

    AppleBootConfigManager mgr;
    mgr.config().devicetree_phys = 0x48000000ULL;
    std::uint32_t dtb_len = 16384;

    AppleBootArgs args = mgr.export_darwin_boot_args(
        0xFFFFFE0007004000ULL,
        0x42000000ULL,
        dtb_len);

    STAGE97_ASSERT(args.Revision == kAppleBootArgsRevision2, "Revision must be Rev 2");
    STAGE97_ASSERT(args.Version == kAppleBootArgsVersion2, "Version must be Ver 2");
    STAGE97_ASSERT(args.physBase == 0x40000000ULL, "Physical RAM base must be 0x40000000");
    STAGE97_ASSERT(args.virtBase == 0xFFFFFE0007004000ULL, "Virtual kernel base must match");
    STAGE97_ASSERT(args.memSize == 6ULL * 1024 * 1024 * 1024, "RAM size must be 6 GB");
    STAGE97_ASSERT(args.deviceTreeP == 0x48000000ULL, "DeviceTree pointer must match");
    STAGE97_ASSERT(args.deviceTreeLength == dtb_len, "DeviceTree length must match");
    STAGE97_ASSERT(args.Video.v_width == 1179 && args.Video.v_height == 2556, "Video width/height must match");
    STAGE97_ASSERT(args.Video.v_rowBytes == 4716, "Video rowBytes must match");
    STAGE97_ASSERT(args.Video.v_depth == 32, "Video depth must be 32-bit");
    STAGE97_ASSERT(std::string(args.CommandLine).find("-v") != std::string::npos, "CommandLine must contain -v");

    std::string err;
    STAGE97_ASSERT(AppleBootArgsBuilder::validate(args, &err), "Exported boot_args must pass validation");

    std::cout << "[PASS] Darwin XNU AppleBootArgs Structure Export\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 6: Key-Value Serialization
// -----------------------------------------------------------------------------
static bool test_key_value_serialization() {
    std::cout << "\n[6] test_key_value_serialization...\n";

    AppleBootConfigManager mgr;
    std::string kv = mgr.serialize_to_key_value();

    STAGE97_ASSERT(kv.find("device_model=iPhone15,4") != std::string::npos, "Must serialize device_model");
    STAGE97_ASSERT(kv.find("board_id=0x18") != std::string::npos, "Must serialize board_id");
    STAGE97_ASSERT(kv.find("chip_id=0x8120") != std::string::npos, "Must serialize chip_id");
    STAGE97_ASSERT(kv.find("cpu_core_count=6") != std::string::npos, "Must serialize cpu_core_count");
    STAGE97_ASSERT(kv.find("display_width=1179") != std::string::npos, "Must serialize display_width");
    STAGE97_ASSERT(kv.find("display_height=2556") != std::string::npos, "Must serialize display_height");

    std::cout << "[PASS] Key-Value Serialization\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 7: Key-Value Deserialization Roundtrip & Error Handling
// -----------------------------------------------------------------------------
static bool test_key_value_deserialization_roundtrip() {
    std::cout << "\n[7] test_key_value_deserialization_roundtrip...\n";

    AppleBootConfigManager mgr_orig(AppleBootConfigManager::create_iphone15_verbose_debug());
    std::string kv = mgr_orig.serialize_to_key_value();

    AppleBootConfigManager mgr_parsed;
    std::string err;
    bool ok = mgr_parsed.deserialize_from_key_value(kv, err);
    STAGE97_ASSERT(ok, "Roundtrip deserialization must succeed");

    const auto& c1 = mgr_orig.config();
    const auto& c2 = mgr_parsed.config();

    STAGE97_ASSERT(c1.device_model == c2.device_model, "device_model must match");
    STAGE97_ASSERT(c1.board_id == c2.board_id, "board_id must match");
    STAGE97_ASSERT(c1.chip_id == c2.chip_id, "chip_id must match");
    STAGE97_ASSERT(c1.ram_base == c2.ram_base, "ram_base must match");
    STAGE97_ASSERT(c1.ram_size == c2.ram_size, "ram_size must match");
    STAGE97_ASSERT(c1.debug_flags == c2.debug_flags, "debug_flags must match");

    // Test malformed value
    std::string bad_kv = "ram_size=not_a_number\n";
    bool bad_ok = mgr_parsed.deserialize_from_key_value(bad_kv, err);
    STAGE97_ASSERT(!bad_ok, "Malformed number must fail deserialization");

    std::cout << "[PASS] Key-Value Deserialization Roundtrip & Error Handling\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 8: Config Markdown Report Generation
// -----------------------------------------------------------------------------
static bool test_config_report_generation() {
    std::cout << "\n[8] test_config_report_generation...\n";

    AppleBootConfigManager mgr;
    std::string report = mgr.generate_config_report();

    STAGE97_ASSERT(!report.empty(), "Report must not be empty");
    STAGE97_ASSERT(report.find("# iPhone 15 Genuine Boot Configuration Report") != std::string::npos,
                   "Header must be present");
    STAGE97_ASSERT(report.find("iPhone15,4") != std::string::npos, "Device model must be in report");
    STAGE97_ASSERT(report.find("1179x2556") != std::string::npos, "Display geometry must be in report");
    STAGE97_ASSERT(report.find("24000000 Hz") != std::string::npos, "Timebase must be in report");

    std::cout << "[PASS] Config Markdown Report Generation\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Running Stage 97: Genuine Boot Configuration Manager Tests\n";
    std::cout << "========================================================\n";

    if (!test_default_iphone15_configuration()) return 1;
    if (!test_command_line_composition()) return 1;
    if (!test_preset_profiles()) return 1;
    if (!test_hardware_profile_validation()) return 1;
    if (!test_darwin_boot_args_export()) return 1;
    if (!test_key_value_serialization()) return 1;
    if (!test_key_value_deserialization_roundtrip()) return 1;
    if (!test_config_report_generation()) return 1;

    std::cout << "\nALL 8 STAGE 97 BOOT CONFIGURATION MANAGER SUITES PASSED!\n";
    return 0;
}
