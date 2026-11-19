#pragma once

#include "packaging/app_package.h"
#include <string>
#include <vector>
#include <sstream>

namespace winios::tools {

struct AppTemplateConfig {
    std::string bundle_id{"com.winios.sampleapp"};
    std::string app_name{"SampleApp"};
    std::string display_name{"Sample Application"};
    std::string version{"1.0.0"};
    std::string category{"Developer"};
    std::string author{"WinIOS Developer"};
    std::string description{"Generated freestanding ARM64 guest application"};
    std::vector<std::string> permissions{"SettingsRead"};
    std::vector<std::string> supported_doc_types{"text/plain"};
    uint32_t    min_sdk_version{0x00020400}; // SDK v2.4
    std::string target_abi{"2.4"};
};

class AppGenerator {
public:
    static std::string generate_c_source(const AppTemplateConfig& cfg) {
        std::ostringstream ss;
        ss << "/*\n";
        ss << " * WinIOS Freestanding ARM64 Application: " << cfg.display_name << "\n";
        ss << " * Bundle ID: " << cfg.bundle_id << "\n";
        ss << " * SDK Version: 0x" << std::hex << cfg.min_sdk_version << std::dec << " (v2.4)\n";
        ss << " */\n\n";
        ss << "#include <stdint.h>\n";
        ss << "#include <stddef.h>\n";
        ss << "#include \"winios/winios.h\"\n\n";
        ss << "int main(int argc, char** argv) {\n";
        ss << "    (void)argc;\n";
        ss << "    (void)argv;\n\n";
        ss << "    winios_log_info(\"" << cfg.app_name << "\", \"Application started successfully\");\n\n";
        ss << "    /* Application workflow logic */\n";
        ss << "    winios_log_info(\"" << cfg.app_name << "\", \"Operations completed. Exiting clean.\");\n";
        ss << "    winios_exit(0);\n";
        ss << "    return 0;\n";
        ss << "}\n";
        return ss.str();
    }

    static std::string generate_manifest_ini(const AppTemplateConfig& cfg) {
        packaging::PackageManifest m{};
        m.format_version = "3.0";
        m.bundle_id = cfg.bundle_id;
        m.name = cfg.app_name;
        m.display_name = cfg.display_name;
        m.version = cfg.version;
        m.min_os_version = "230.0";
        m.architecture = "arm64";
        m.min_abi_version = "2.4";
        m.min_sdk_version_u32 = cfg.min_sdk_version;
        m.target_abi_version = cfg.target_abi;
        m.executable_name = "bin/" + cfg.app_name;
        m.category = cfg.category;
        m.author = cfg.author;
        m.description = cfg.description;
        m.permissions = cfg.permissions;
        m.supported_doc_types = cfg.supported_doc_types;
        return m.serialize();
    }

    static std::string generate_build_script(const AppTemplateConfig& cfg) {
        std::ostringstream ss;
        ss << "# Build script for " << cfg.app_name << "\n";
        ss << "zig cc -target aarch64-freestanding -c main.c -I<SDK_INCLUDE_PATH> -O2 -ffreestanding\n";
        ss << "zig ld.lld -marm64_ios -static -e _main main.o <SDK_LIB_PATH>/winios_runtime.o -o bin/" << cfg.app_name << "\n";
        return ss.str();
    }

    static packaging::AppPackage generate_package(const AppTemplateConfig& cfg, const std::vector<uint8_t>& compiled_binary) {
        packaging::AppPackage pkg{};
        pkg.manifest.format_version = "3.0";
        pkg.manifest.bundle_id = cfg.bundle_id;
        pkg.manifest.name = cfg.app_name;
        pkg.manifest.display_name = cfg.display_name;
        pkg.manifest.version = cfg.version;
        pkg.manifest.min_os_version = "230.0";
        pkg.manifest.architecture = "arm64";
        pkg.manifest.min_abi_version = "2.4";
        pkg.manifest.min_sdk_version_u32 = cfg.min_sdk_version;
        pkg.manifest.target_abi_version = cfg.target_abi;
        pkg.manifest.executable_name = "bin/" + cfg.app_name;
        pkg.manifest.category = cfg.category;
        pkg.manifest.author = cfg.author;
        pkg.manifest.description = cfg.description;
        pkg.manifest.permissions = cfg.permissions;
        pkg.manifest.supported_doc_types = cfg.supported_doc_types;

        pkg.add_file("bin/" + cfg.app_name, compiled_binary);
        pkg.add_file("res/info.json", {'{', '\"', 'a', 'p', 'p', '\"', ':', ' ', '\"', 'o', 'k', '\"', '}', '\n'});
        return pkg;
    }
};

} // namespace winios::tools
