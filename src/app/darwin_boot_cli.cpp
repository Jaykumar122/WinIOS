#include "app/darwin_boot_cli.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "loader/macho_loader.h"

namespace winios::app {

void DarwinBootCli::print_usage() {
    std::cout << "WinIOS Darwin / iOS Virtual Machine Boot CLI\n"
              << "Target Architecture: iPhone 15 (iPhone15,4 / Apple A16 Bionic T8120)\n\n"
              << "Usage:\n"
              << "  win_ios.exe darwin boot --kernelcache <path> [options]\n"
              << "  win_ios.exe darwin info --kernelcache <path>\n\n"
              << "Options:\n"
              << "  -k, --kernelcache <path>  Path to decrypted iOS kernelcache Mach-O (required)\n"
              << "  -a, --append <args>       Kernel boot arguments (default: \"-v debug=0x144 serial=3 keepsyms=1\")\n"
              << "      --memory <MB>         Guest RAM size in MB (default: 256 MB, up to 6144 MB)\n"
              << "      --smp <cores>         Number of virtual CPU cores (default: 6: 2 Everest + 4 Sawtooth)\n"
              << "      --max-insns <count>   Instruction execution budget limit (default: 10000000)\n"
              << "      --display             Open native DirectX / DIBSection display window (Super Retina XDR scaled)\n"
              << "      --headless            Run in headless offscreen mode (default)\n"
              << "      --screenshot <path>   Save 32-bit BMP screenshot of virtual screen upon completion\n"
              << "      --console-log <file>  Export UART console output to log file\n"
              << "  -h, --help                Show this help message\n";
}

bool DarwinBootCli::parse_arguments(
    int argc,
    char* const argv[],
    DarwinBootCliConfig& out_config,
    std::string& out_error) {

    int start_index = 1;
    // Check if invoked via "darwin boot" or "darwin info" or directly
    if (start_index < argc && std::string_view(argv[start_index]) == "darwin") {
        start_index++;
    }

    if (start_index < argc) {
        std::string_view subcmd = argv[start_index];
        if (subcmd == "boot") {
            out_config.info_only = false;
            start_index++;
        } else if (subcmd == "info") {
            out_config.info_only = true;
            start_index++;
        } else if (subcmd == "-h" || subcmd == "--help") {
            out_config.help_requested = true;
            print_usage();
            return true;
        }
    }

    for (int i = start_index; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            out_config.help_requested = true;
            print_usage();
            return true;
        } else if (arg == "-k" || arg == "--kernelcache" || arg == "--kernel") {
            if (i + 1 >= argc) {
                out_error = "--kernelcache requires a path argument";
                return false;
            }
            out_config.session_config.kernelcache_path = argv[++i];
        } else if (arg == "-a" || arg == "--append") {
            if (i + 1 >= argc) {
                out_error = "--append requires a boot arguments string";
                return false;
            }
            out_config.session_config.boot_args = argv[++i];
        } else if (arg == "--memory" || arg == "--ram") {
            if (i + 1 >= argc) {
                out_error = "--memory requires a size in MB";
                return false;
            }
            long mb = std::strtol(argv[++i], nullptr, 10);
            if (mb <= 0) {
                out_error = "Invalid RAM size: " + std::string(argv[i]);
                return false;
            }
            out_config.session_config.ram_size = static_cast<std::size_t>(mb) * 1024ULL * 1024ULL;
        } else if (arg == "--smp") {
            if (i + 1 >= argc) {
                out_error = "--smp requires core count";
                return false;
            }
            long c = std::strtol(argv[++i], nullptr, 10);
            if (c <= 0 || c > 8) {
                out_error = "Invalid core count (must be 1..8): " + std::string(argv[i]);
                return false;
            }
            out_config.session_config.core_count = static_cast<std::size_t>(c);
        } else if (arg == "--max-insns") {
            if (i + 1 >= argc) {
                out_error = "--max-insns requires count";
                return false;
            }
            out_config.session_config.max_instructions = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--display") {
            out_config.show_display = true;
            out_config.headless = false;
        } else if (arg == "--headless") {
            out_config.show_display = false;
            out_config.headless = true;
        } else if (arg == "--screenshot") {
            if (i + 1 >= argc) {
                out_error = "--screenshot requires a destination file path";
                return false;
            }
            out_config.screenshot_path = argv[++i];
        } else if (arg == "--console-log") {
            if (i + 1 >= argc) {
                out_error = "--console-log requires a file path";
                return false;
            }
            out_config.session_config.console_log_path = argv[++i];
        } else {
            out_error = "Unrecognized option: " + std::string(arg);
            return false;
        }
    }

    if (out_config.session_config.kernelcache_path.empty() && out_config.session_config.kernelcache_data.empty()) {
        out_error = "Missing required argument: --kernelcache <path>";
        return false;
    }

    return true;
}

int DarwinBootCli::run_info(const std::string& kernelcache_path) {
    std::ifstream file(kernelcache_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open kernelcache file: " << kernelcache_path << "\n";
        return 1;
    }

    std::streamsize sz = file.tellg();
    if (sz <= 0) {
        std::cerr << "Error: Kernelcache file is empty: " << kernelcache_path << "\n";
        return 1;
    }

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(sz));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), sz)) {
        std::cerr << "Error: Failed reading kernelcache file: " << kernelcache_path << "\n";
        return 1;
    }

    loader::MachOLoadResult result{};
    if (!loader::MachOLoader::parse_metadata(buffer.data(), buffer.size(), result)) {
        std::cerr << "Error: Failed parsing Mach-O kernelcache: " << result.error_message << "\n";
        return 2;
    }

    std::cout << "=== iOS Kernelcache Metadata Inspection ===\n"
              << "Path:              " << kernelcache_path << "\n"
              << "File Size:         " << sz << " bytes (" << (sz / (1024 * 1024)) << " MB)\n"
              << "Magic:             0x" << std::hex << result.header.magic << std::dec << "\n"
              << "CPU Type:          0x" << std::hex << result.header.cputype
              << " (ARM64)\n" << std::dec
              << "CPU Subtype:       0x" << std::hex << result.header.cpusubtype << std::dec << "\n"
              << "File Type:         " << (result.header.filetype == loader::kMachOFileTypeFileset ? "MH_FILESET (0xC)" : "MH_EXECUTE (0x2)") << "\n"
              << "Load Commands:     " << result.header.ncmds << " (" << result.header.sizeofcmds << " bytes)\n"
              << "Total Segments:    " << result.segments.size() << "\n"
              << "Fileset Entries:   " << result.fileset_entries.size() << "\n"
              << "Lowest VM Address: 0x" << std::hex << result.lowest_vmaddr << "\n"
              << "Highest VM Addr:   0x" << result.highest_vmaddr << "\n"
              << "Total VM Size:     " << std::dec << (result.total_vmsize / (1024 * 1024)) << " MB (0x" << std::hex << result.total_vmsize << " bytes)\n"
              << "Entry Point:       0x" << result.entry_point << std::dec << "\n\n";

    std::cout << "--- Segments ---\n";
    for (const auto& s : result.segments) {
        std::cout << "  Segment: " << std::left << std::setw(16) << s.name
                  << " VM: 0x" << std::hex << s.vmaddr << " - 0x" << (s.vmaddr + s.vmsize)
                  << " (Size: 0x" << s.vmsize << ", FileOff: 0x" << s.fileoff << ", FileSz: 0x" << s.filesize << ")"
                  << std::dec << "\n";
    }

    if (!result.fileset_entries.empty()) {
        std::cout << "\n--- Fileset Entries ---\n";
        for (const auto& fe : result.fileset_entries) {
            std::cout << "  Entry: " << std::left << std::setw(32) << fe.entry_id
                      << " VM: 0x" << std::hex << fe.vmaddr
                      << " (FileOff: 0x" << fe.fileoff << ")"
                      << std::dec << "\n";
        }
    }

    return 0;
}

int DarwinBootCli::run_boot(const DarwinBootCliConfig& config) {
    runtime::DarwinBootSession session(config.session_config);
    std::string err;

    std::cout << "Initializing WinIOS Darwin Session for iPhone 15 (iPhone15,4 / Apple A16 Bionic T8120)...\n";
    if (!session.initialize(err)) {
        std::cerr << "Initialization failed: " << err << "\n";
        return 2;
    }

    std::cout << "Preparing Darwin Boot Handoff (DeviceTree & struct boot_args)...\n";
    if (!session.prepare_handoff(err)) {
        std::cerr << "Boot handoff preparation failed: " << err << "\n";
        return 3;
    }

    std::cout << "Target Profile: iPhone 15 (Board ID 0x18, Chip ID 0x8120)\n"
              << "CPUs:           " << config.session_config.core_count << " Cores (2 Everest + 4 Sawtooth)\n"
              << "RAM:            " << (config.session_config.ram_size / (1024 * 1024)) << " MB at 0x" << std::hex << config.session_config.ram_base << std::dec << "\n"
              << "Boot Args:      \"" << config.session_config.boot_args << "\"\n"
              << "BootArgs Ptr:   0x" << std::hex << session.boot_args_phys_address() << std::dec << "\n"
              << "DeviceTree Ptr: 0x" << std::hex << session.devicetree_phys_address() << " (" << session.devicetree_length() << " bytes)\n" << std::dec
              << "Display:        " << (config.show_display ? "DirectX Window Active" : "Headless Offscreen") << " (1179 x 2556 Super Retina XDR)\n\n";

    ui::DisplayWindow display_win({
        "WinIOS - iPhone 15 (iPhone15,4 / Apple A16 Bionic)",
        device::AppleDisplay::kDefaultWidth,
        device::AppleDisplay::kDefaultHeight,
        0.35f,
        config.headless,
        true
    });

    if (config.show_display) {
        if (display_win.create()) {
            display_win.present_frame(session.display());
            display_win.pump_events();
        } else {
            std::cerr << "Warning: Could not create display window; falling back to headless mode.\n";
        }
    }

    std::cout << "Executing Darwin boot sequence...\n";

    // Step in chunks to allow display frame refresh and message loop processing
    const std::size_t chunk_size = 50000;
    std::size_t total_budget = config.session_config.max_instructions;
    std::size_t executed = 0;

    while (executed < total_budget && session.status() == runtime::DarwinSessionStatus::Running) {
        std::size_t step_insns = std::min(chunk_size, total_budget - executed);
        std::size_t done = session.step(step_insns);
        executed += done;

        if (config.show_display && display_win.is_open()) {
            display_win.present_frame(session.display());
            if (!display_win.pump_events()) {
                std::cout << "Display window closed by user.\n";
                break;
            }
        }

        if (done == 0) {
            break;
        }
    }

    std::cout << "Session completed. Total instructions executed: " << session.total_instructions_executed() << "\n";

    if (!config.screenshot_path.empty()) {
        std::cout << "Exporting screen capture to: " << config.screenshot_path << "...\n";
        bool saved = false;
        if (display_win.is_open()) {
            saved = display_win.save_screenshot_bmp(config.screenshot_path);
        } else {
            ui::DisplayWindow offscreen({ "", device::AppleDisplay::kDefaultWidth, device::AppleDisplay::kDefaultHeight, 1.0f, true, true });
            offscreen.present_frame(session.display());
            saved = offscreen.save_screenshot_bmp(config.screenshot_path);
        }

        if (saved) {
            std::cout << "Screenshot saved successfully (" << config.screenshot_path << ").\n";
        } else {
            std::cerr << "Warning: Failed to save screenshot.\n";
        }
    }

    if (!config.session_config.console_log_path.empty()) {
        std::ofstream log_file(config.session_config.console_log_path);
        if (log_file.is_open()) {
            log_file << session.get_console_output();
            std::cout << "Console log written to: " << config.session_config.console_log_path << "\n";
        }
    }

    std::string console_out = session.get_console_output();
    if (!console_out.empty()) {
        std::cout << "\n=== Console UART Output ===\n" << console_out << "\n";
    }

    return 0;
}

int DarwinBootCli::run(int argc, char* const argv[]) {
    DarwinBootCliConfig config;
    std::string err;

    if (!parse_arguments(argc, argv, config, err)) {
        if (!err.empty()) {
            std::cerr << "Error: " << err << "\n\n";
            print_usage();
        }
        return 1;
    }

    if (config.help_requested) {
        return 0;
    }

    if (config.info_only) {
        return run_info(config.session_config.kernelcache_path);
    }

    return run_boot(config);
}

} // namespace winios::app
