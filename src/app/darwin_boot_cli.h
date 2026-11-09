#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "runtime/darwin_boot_session.h"
#include "ui/display_window.h"

namespace winios::app {

// -----------------------------------------------------------------------------
// DarwinBootCliConfig: Options parsed from command-line arguments
// -----------------------------------------------------------------------------
struct DarwinBootCliConfig {
    runtime::DarwinBootConfig session_config;
    bool show_display{false};
    bool headless{true};
    std::string screenshot_path;
    bool info_only{false};
    bool help_requested{false};
};

// -----------------------------------------------------------------------------
// DarwinBootCli: Command-line interface for booting Darwin / iOS on iPhone 15
// -----------------------------------------------------------------------------
class DarwinBootCli {
public:
    static void print_usage();

    static bool parse_arguments(
        int argc,
        char* const argv[],
        DarwinBootCliConfig& out_config,
        std::string& out_error);

    static int run(int argc, char* const argv[]);

    static int run_info(const std::string& kernelcache_path);
    static int run_boot(const DarwinBootCliConfig& config);
};

} // namespace winios::app
