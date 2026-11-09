#pragma once

#include <string>
#include "runtime/linux_boot_session.h"

namespace winios::app {

class LinuxBootCli {
public:
    static bool parse_arguments(
        int argc,
        char* const argv[],
        runtime::LinuxBootConfig& out_config,
        std::string& out_error);

    static int run(int argc, char* const argv[]);

    static void print_usage();
};

} // namespace winios::app
