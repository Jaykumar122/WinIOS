#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "app/darwin_boot_cli.h"
#include "device/apple_display.h"
#include "loader/macho_loader.h"

#define CLI_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::app;
using namespace winios::loader;

namespace {

// Helper to write a synthetic minimal ARM64 Mach-O kernelcache file
std::string create_temp_macho_file(const std::string& filename) {
    std::vector<std::uint8_t> buf(0x8000, 0);
    auto* hdr = reinterpret_cast<MachOHeader64*>(buf.data());
    hdr->magic = kMachOMagic64;
    hdr->cputype = kCpuTypeARM64;
    hdr->cpusubtype = kCpuSubtypeARM64All;
    hdr->filetype = kMachOFileTypeFileset;
    hdr->ncmds = 2; // LC_SEGMENT_64 + LC_FILESET_ENTRY

    const std::uint32_t seg_cmdsize = sizeof(MachOSegmentCommand64);
    const std::uint32_t fse_cmdsize = sizeof(MachOFilesetEntryCommand) + 24;
    hdr->sizeofcmds = seg_cmdsize + fse_cmdsize;

    std::size_t offset = sizeof(MachOHeader64);

    // 1. LC_SEGMENT_64 (__TEXT_EXEC)
    auto* seg = reinterpret_cast<MachOSegmentCommand64*>(buf.data() + offset);
    seg->cmd = kLoadCmdSegment64;
    seg->cmdsize = seg_cmdsize;
    std::memcpy(seg->segname, "__TEXT_EXEC", 11);
    seg->vmaddr = 0xFFFFFFF007004000ULL;
    seg->vmsize = 0x4000;
    seg->fileoff = 0x1000;
    seg->filesize = 0x1000;
    seg->maxprot = kVmProtRead | kVmProtExecute;
    seg->initprot = kVmProtRead | kVmProtExecute;
    offset += seg_cmdsize;

    // 2. LC_FILESET_ENTRY (com.apple.kernel)
    auto* fse = reinterpret_cast<MachOFilesetEntryCommand*>(buf.data() + offset);
    fse->cmd = kLoadCmdFilesetEntry;
    fse->cmdsize = fse_cmdsize;
    fse->vmaddr = 0xFFFFFFF007004000ULL;
    fse->fileoff = 0x1000;
    fse->entry_id_offset = sizeof(MachOFilesetEntryCommand);
    const char* kext_id = "com.apple.kernel";
    std::memcpy(buf.data() + offset + sizeof(MachOFilesetEntryCommand), kext_id, std::strlen(kext_id) + 1);

    // Instructions at fileoff 0x1000:
    // 0xD503201F = NOP
    // 0xD65F03C0 = RET
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1000) = 0xD503201Fu;
    *reinterpret_cast<std::uint32_t*>(buf.data() + 0x1004) = 0xD65F03C0u;

    std::ofstream out(filename, std::ios::binary);
    out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    out.close();

    return filename;
}

// -----------------------------------------------------------------------------
// Suite 1: Argument Parsing Defaults
// -----------------------------------------------------------------------------
bool test_argument_parsing_defaults() {
    std::cout << "[1] test_argument_parsing_defaults...\n";

    char arg0[] = "win_ios.exe";
    char arg1[] = "darwin";
    char arg2[] = "boot";
    char arg3[] = "--kernelcache";
    char arg4[] = "kernelcache.arm64";
    char* argv[] = { arg0, arg1, arg2, arg3, arg4 };
    int argc = 5;

    DarwinBootCliConfig config;
    std::string err;
    bool ok = DarwinBootCli::parse_arguments(argc, argv, config, err);

    CLI_ASSERT(ok, "Arguments parsed successfully");
    CLI_ASSERT(err.empty(), "No errors reported");
    CLI_ASSERT(config.session_config.kernelcache_path == "kernelcache.arm64", "Kernelcache path matches");
    CLI_ASSERT(config.session_config.core_count == 6, "Default 6 cores for iPhone 15");
    CLI_ASSERT(config.session_config.ram_size == 0x10000000ULL, "Default 256 MB RAM");
    CLI_ASSERT(config.session_config.boot_args == "-v debug=0x144 serial=3 keepsyms=1", "Default boot args");
    CLI_ASSERT(config.headless == true, "Headless by default");
    CLI_ASSERT(config.show_display == false, "Display disabled by default");
    CLI_ASSERT(config.info_only == false, "Info only false");

    std::cout << "[PASS] Argument Parsing Defaults\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Custom Argument Configuration
// -----------------------------------------------------------------------------
bool test_argument_parsing_custom_options() {
    std::cout << "[2] test_argument_parsing_custom_options...\n";

    char arg0[] = "win_ios.exe";
    char arg1[] = "darwin";
    char arg2[] = "boot";
    char arg3[] = "-k";
    char arg4[] = "custom_kc.bin";
    char arg5[] = "-a";
    char arg6[] = "-v keepsyms=1 cpus=2";
    char arg7[] = "--memory";
    char arg8[] = "512";
    char arg9[] = "--smp";
    char arg10[] = "4";
    char arg11[] = "--max-insns";
    char arg12[] = "500000";
    char arg13[] = "--display";
    char arg14[] = "--screenshot";
    char arg15[] = "test_screen.bmp";
    char arg16[] = "--console-log";
    char arg17[] = "test_console.log";

    char* argv[] = {
        arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8,
        arg9, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    DarwinBootCliConfig config;
    std::string err;
    bool ok = DarwinBootCli::parse_arguments(argc, argv, config, err);

    CLI_ASSERT(ok, "Custom arguments parsed successfully");
    CLI_ASSERT(config.session_config.kernelcache_path == "custom_kc.bin", "Kernelcache path matches");
    CLI_ASSERT(config.session_config.boot_args == "-v keepsyms=1 cpus=2", "Custom boot args match");
    CLI_ASSERT(config.session_config.ram_size == 512 * 1024 * 1024ULL, "512 MB RAM parsed");
    CLI_ASSERT(config.session_config.core_count == 4, "4 SMP cores parsed");
    CLI_ASSERT(config.session_config.max_instructions == 500000, "500k instructions parsed");
    CLI_ASSERT(config.show_display == true, "Display enabled");
    CLI_ASSERT(config.headless == false, "Headless false");
    CLI_ASSERT(config.screenshot_path == "test_screen.bmp", "Screenshot path stored");
    CLI_ASSERT(config.session_config.console_log_path == "test_console.log", "Console log path stored");

    std::cout << "[PASS] Custom Argument Configuration\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Info Subcommand Argument Parsing
// -----------------------------------------------------------------------------
bool test_argument_parsing_info_subcommand() {
    std::cout << "[3] test_argument_parsing_info_subcommand...\n";

    char arg0[] = "win_ios.exe";
    char arg1[] = "darwin";
    char arg2[] = "info";
    char arg3[] = "--kernelcache";
    char arg4[] = "release_kernelcache";
    char* argv[] = { arg0, arg1, arg2, arg3, arg4 };
    int argc = 5;

    DarwinBootCliConfig config;
    std::string err;
    bool ok = DarwinBootCli::parse_arguments(argc, argv, config, err);

    CLI_ASSERT(ok, "Info arguments parsed");
    CLI_ASSERT(config.info_only == true, "info_only is true");
    CLI_ASSERT(config.session_config.kernelcache_path == "release_kernelcache", "Path matches");

    std::cout << "[PASS] Info Subcommand Argument Parsing\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Argument Parsing Error Handling
// -----------------------------------------------------------------------------
bool test_argument_parsing_errors() {
    std::cout << "[4] test_argument_parsing_errors...\n";

    // 1. Missing --kernelcache
    {
        char arg0[] = "win_ios.exe";
        char arg1[] = "darwin";
        char arg2[] = "boot";
        char* argv[] = { arg0, arg1, arg2 };
        DarwinBootCliConfig cfg;
        std::string err;
        CLI_ASSERT(!DarwinBootCli::parse_arguments(3, argv, cfg, err), "Fails without --kernelcache");
        CLI_ASSERT(err.find("Missing required argument") != std::string::npos, "Helpful missing argument message");
    }

    // 2. Invalid RAM
    {
        char arg0[] = "win_ios.exe";
        char arg1[] = "darwin";
        char arg2[] = "boot";
        char arg3[] = "-k";
        char arg4[] = "kc.bin";
        char arg5[] = "--memory";
        char arg6[] = "0";
        char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
        DarwinBootCliConfig cfg;
        std::string err;
        CLI_ASSERT(!DarwinBootCli::parse_arguments(7, argv, cfg, err), "Fails on RAM 0");
        CLI_ASSERT(err.find("Invalid RAM size") != std::string::npos, "Error reports invalid RAM");
    }

    // 3. Invalid SMP cores (max is 8)
    {
        char arg0[] = "win_ios.exe";
        char arg1[] = "darwin";
        char arg2[] = "boot";
        char arg3[] = "-k";
        char arg4[] = "kc.bin";
        char arg5[] = "--smp";
        char arg6[] = "16";
        char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
        DarwinBootCliConfig cfg;
        std::string err;
        CLI_ASSERT(!DarwinBootCli::parse_arguments(7, argv, cfg, err), "Fails on SMP > 8");
        CLI_ASSERT(err.find("Invalid core count") != std::string::npos, "Error reports invalid cores");
    }

    // 4. Unknown option
    {
        char arg0[] = "win_ios.exe";
        char arg1[] = "darwin";
        char arg2[] = "boot";
        char arg3[] = "-k";
        char arg4[] = "kc.bin";
        char arg5[] = "--unknown-option";
        char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5 };
        DarwinBootCliConfig cfg;
        std::string err;
        CLI_ASSERT(!DarwinBootCli::parse_arguments(6, argv, cfg, err), "Fails on unknown option");
        CLI_ASSERT(err.find("Unrecognized option") != std::string::npos, "Reports unrecognized option");
    }

    std::cout << "[PASS] Argument Parsing Error Handling\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Kernelcache Info Inspection via CLI
// -----------------------------------------------------------------------------
bool test_cli_info_run_synthetic_fileset() {
    std::cout << "[5] test_cli_info_run_synthetic_fileset...\n";

    std::string temp_file = "test_synthetic_info_kc.bin";
    create_temp_macho_file(temp_file);

    int rc = DarwinBootCli::run_info(temp_file);
    CLI_ASSERT(rc == 0, "run_info succeeded with 0 exit code");

    // Clean up
    std::filesystem::remove(temp_file);

    std::cout << "[PASS] Kernelcache Info Inspection via CLI\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Boot Run and Screenshot Capture via CLI
// -----------------------------------------------------------------------------
bool test_cli_boot_run_headless_synthetic_kernel() {
    std::cout << "[6] test_cli_boot_run_headless_synthetic_kernel...\n";

    std::string temp_kc = "test_synthetic_boot_kc.bin";
    create_temp_macho_file(temp_kc);

    std::string screenshot_path = "test_cli_output.bmp";
    std::filesystem::remove(screenshot_path);

    DarwinBootCliConfig cfg;
    cfg.session_config.kernelcache_path = temp_kc;
    cfg.session_config.max_instructions = 10;
    cfg.session_config.ram_size = 0x10000000;
    cfg.headless = true;
    cfg.show_display = false;
    cfg.screenshot_path = screenshot_path;

    int rc = DarwinBootCli::run_boot(cfg);
    CLI_ASSERT(rc == 0, "run_boot succeeded with 0 exit code");

    CLI_ASSERT(std::filesystem::exists(screenshot_path), "Screenshot BMP was written to disk");
    std::uintmax_t bmp_size = std::filesystem::file_size(screenshot_path);
    CLI_ASSERT(bmp_size > 54, "BMP file size greater than header size");

    // Verify BMP Header
    std::ifstream in(screenshot_path, std::ios::binary);
    char magic[2];
    in.read(magic, 2);
    CLI_ASSERT(magic[0] == 'B' && magic[1] == 'M', "BMP magic valid");

    in.close();
    std::filesystem::remove(temp_kc);
    std::filesystem::remove(screenshot_path);

    std::cout << "[PASS] Boot Run and Screenshot Capture via CLI\n\n";
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Full End-to-End CLI Dispatch
// -----------------------------------------------------------------------------
bool test_cli_full_dispatch_main() {
    std::cout << "[7] test_cli_full_dispatch_main...\n";

    std::string temp_kc = "test_dispatch_kc.bin";
    create_temp_macho_file(temp_kc);

    char arg0[] = "win_ios.exe";
    char arg1[] = "darwin";
    char arg2[] = "boot";
    char arg3[] = "--kernelcache";
    char arg4[256];
    std::snprintf(arg4, sizeof(arg4), "%s", temp_kc.c_str());
    char arg5[] = "--headless";
    char arg6[] = "--max-insns";
    char arg7[] = "20";

    char* argv[] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    int argc = 8;

    int rc = DarwinBootCli::run(argc, argv);
    CLI_ASSERT(rc == 0, "DarwinBootCli::run returned 0 for valid boot");

    std::filesystem::remove(temp_kc);

    std::cout << "[PASS] Full End-to-End CLI Dispatch\n\n";
    return true;
}

} // anonymous namespace

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "=== WinIOS Darwin Boot CLI Tests ===\n\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](bool (*test_fn)(), const char* name) {
        std::cout << "Starting " << name << "...\n";
        if (test_fn()) {
            passed++;
            std::cout << "Completed " << name << ": OK\n";
        } else {
            failed++;
            std::cerr << "[FAIL] " << name << "\n";
        }
    };

    run_test(test_argument_parsing_defaults, "test_argument_parsing_defaults");
    run_test(test_argument_parsing_custom_options, "test_argument_parsing_custom_options");
    run_test(test_argument_parsing_info_subcommand, "test_argument_parsing_info_subcommand");
    run_test(test_argument_parsing_errors, "test_argument_parsing_errors");
    run_test(test_cli_info_run_synthetic_fileset, "test_cli_info_run_synthetic_fileset");
    run_test(test_cli_boot_run_headless_synthetic_kernel, "test_cli_boot_run_headless_synthetic_kernel");
    run_test(test_cli_full_dispatch_main, "test_cli_full_dispatch_main");

    std::cout << "\nDarwin Boot CLI Summary: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
