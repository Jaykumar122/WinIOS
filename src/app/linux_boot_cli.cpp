#include "app/linux_boot_cli.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace winios::app {

void LinuxBootCli::print_usage() {
    std::cout << "WinIOS Linux ARM64 Virtual Machine Boot CLI\n"
              << "Usage: win_ios_boot --kernel <path> [options]\n\n"
              << "Options:\n"
              << "  --kernel <path>       Path to uncompressed Linux ARM64 Image (required)\n"
              << "  --dtb <path>          Path to external DTB blob (optional, default auto-synthesized)\n"
              << "  --initrd <path>       Path to initramfs / root filesystem archive (optional)\n"
              << "  --append <args>       Kernel command-line boot arguments\n"
              << "  --memory <MB>         Guest RAM size in MB (default: 128 MB)\n"
              << "  --smp <cores>         Number of virtual CPU cores (default: 1, max: 8)\n"
              << "  --max-insns <count>   Instruction execution budget limit (default: 10000000)\n"
              << "  --console-log <file>  Export UART console output to log file\n"
              << "  -h, --help            Show this help message\n";
}

bool LinuxBootCli::parse_arguments(
    int argc,
    char* const argv[],
    runtime::LinuxBootConfig& out_config,
    std::string& out_error) {

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage();
            return false;
        } else if (arg == "--kernel") {
            if (i + 1 >= argc) {
                out_error = "--kernel requires a path argument";
                return false;
            }
            out_config.kernel_path = argv[++i];
        } else if (arg == "--dtb") {
            if (i + 1 >= argc) {
                out_error = "--dtb requires a path argument";
                return false;
            }
            out_config.dtb_path = argv[++i];
        } else if (arg == "--initrd") {
            if (i + 1 >= argc) {
                out_error = "--initrd requires a path argument";
                return false;
            }
            out_config.initramfs_path = argv[++i];
        } else if (arg == "--append") {
            if (i + 1 >= argc) {
                out_error = "--append requires a boot arguments string";
                return false;
            }
            out_config.boot_args = argv[++i];
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
            out_config.ram_size = static_cast<std::size_t>(mb) * 1024ULL * 1024ULL;
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
            out_config.core_count = static_cast<std::size_t>(c);
        } else if (arg == "--max-insns") {
            if (i + 1 >= argc) {
                out_error = "--max-insns requires count";
                return false;
            }
            out_config.max_instructions = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--console-log") {
            if (i + 1 >= argc) {
                out_error = "--console-log requires a file path";
                return false;
            }
            out_config.console_log_path = argv[++i];
        } else {
            out_error = "Unrecognized option: " + std::string(arg);
            return false;
        }
    }

    if (out_config.kernel_path.empty() && out_config.kernel_data.empty()) {
        out_error = "Missing required argument: --kernel <path>";
        return false;
    }

    return true;
}

int LinuxBootCli::run(int argc, char* const argv[]) {
    runtime::LinuxBootConfig config;
    std::string err;

    if (!parse_arguments(argc, argv, config, err)) {
        if (!err.empty()) {
            std::cerr << "Error: " << err << "\n";
            print_usage();
        }
        return 1;
    }

    runtime::LinuxBootSession session(config);
    if (!session.initialize(err)) {
        std::cerr << "Boot initialization failed: " << err << "\n";
        return 2;
    }

    if (!session.boot(err)) {
        std::cerr << "Boot handoff failed: " << err << "\n";
        return 3;
    }

    std::cout << "Booting Linux VM (" << config.core_count << " CPUs, "
              << (config.ram_size / (1024 * 1024)) << " MB RAM)...\n";

    session.run();

    std::cout << "\n=== Console Output ===\n" << session.get_console_output() << "\n";
    std::cout << "Executed " << session.total_instructions_executed() << " instructions.\n";
    return 0;
}

} // namespace winios::app
