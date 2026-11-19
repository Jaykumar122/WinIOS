#pragma once

#include "packaging/app_package.h"
#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>

namespace winios::tools {

class WiniosAppCli {
public:
    WiniosAppCli(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                 std::shared_ptr<packaging::ApplicationRegistry> registry,
                 std::shared_ptr<packaging::PackageInstaller> installer,
                 std::shared_ptr<loader::AppLaunchPipeline> launcher,
                 std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr,
                 std::shared_ptr<services::DiagnosticsService> diag_svc)
        : vfs_(std::move(vfs)),
          registry_(std::move(registry)),
          installer_(std::move(installer)),
          launcher_(std::move(launcher)),
          identity_mgr_(std::move(identity_mgr)),
          diag_svc_(std::move(diag_svc)) {}

    ~WiniosAppCli() = default;

    int execute(const std::vector<std::string>& args, std::ostream& out = std::cout, std::ostream& err = std::cerr) {
        if (args.empty()) {
            print_help(out);
            return 0;
        }

        const std::string& cmd = args[0];
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            print_help(out);
            return 0;
        }

        if (cmd == "app-list") {
            bool active_only = (args.size() > 1 && args[1] == "--active-only");
            if (!registry_) {
                err << "Error: Application registry not available\n";
                return 1;
            }
            auto apps = registry_->list_apps(active_only);
            out << "Installed Applications (" << apps.size() << "):\n";
            for (const auto& a : apps) {
                out << "  " << a.bundle_id << " [" << a.name << "] v" << a.version
                    << " (State: " << packaging::installation_state_to_string(a.state)
                    << ", Active: " << (a.is_active ? "yes" : "no") << ")\n";
            }
            return 0;
        }

        if (cmd == "app-info") {
            if (args.size() < 2) {
                err << "Usage: app-info <bundle_id>\n";
                return 1;
            }
            const std::string& bid = args[1];
            auto a_opt = registry_ ? registry_->get_app(bid) : std::nullopt;
            if (!a_opt.has_value()) {
                err << "Error: Application " << bid << " not found\n";
                return 1;
            }
            const auto& a = *a_opt;
            out << "Application Information: " << a.bundle_id << "\n"
                << "  Name:         " << a.name << "\n"
                << "  Display Name: " << a.display_name << "\n"
                << "  Version:      " << a.version << " (Pkg v" << a.package_version << ")\n"
                << "  Min SDK:      0x" << std::hex << a.min_sdk_version << std::dec << "\n"
                << "  Target ABI:   " << a.target_abi << "\n"
                << "  Install Dir:  " << a.install_dir << "\n"
                << "  Executable:   " << a.exec_path << "\n"
                << "  State:        " << packaging::installation_state_to_string(a.state) << "\n"
                << "  Storage Quota:" << a.storage_quota_bytes << " bytes\n";
            return 0;
        }

        if (cmd == "app-status") {
            if (args.size() < 2) {
                err << "Usage: app-status <bundle_id>\n";
                return 1;
            }
            const std::string& bid = args[1];
            if (!identity_mgr_) {
                err << "Error: Process identity manager not available\n";
                return 1;
            }
            auto active_list = identity_mgr_->list_active_instances();
            bool running = false;
            for (const auto& inst : active_list) {
                if (inst.app.bundle_id == bid && inst.active) {
                    out << "Application " << bid << " is RUNNING:\n"
                        << "  PID:        " << inst.handle.pid << "\n"
                        << "  Generation: " << inst.handle.generation << "\n"
                        << "  Instance ID:" << inst.instance_id << "\n";
                    running = true;
                    break;
                }
            }
            if (!running) {
                out << "Application " << bid << " is STOPPED\n";
            }
            return 0;
        }

        if (cmd == "pkg-inspect") {
            if (args.size() < 2) {
                err << "Usage: pkg-inspect <pkg_path>\n";
                return 1;
            }
            const std::string& path = args[1];
            std::vector<uint8_t> data;
            if (!vfs_ || !vfs_->read_file(path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << path << "\n";
                return 1;
            }
            packaging::AppPackage pkg{};
            auto val = packaging::AppPackage::validate_package_data(data.data(), data.size(), &pkg);
            out << "Package Inspection: " << path << " (" << data.size() << " bytes)\n"
                << "  Validation:   " << packaging::package_validation_result_to_string(val) << "\n";
            if (val == packaging::PackageValidationResult::Valid) {
                out << "  Bundle ID:    " << pkg.manifest.bundle_id << "\n"
                    << "  Name:         " << pkg.manifest.name << " (" << pkg.manifest.display_name << ")\n"
                    << "  Version:      " << pkg.manifest.version << " [Format " << pkg.manifest.format_version << "]\n"
                    << "  Executable:   " << pkg.manifest.executable_name << "\n"
                    << "  Entries:      " << pkg.manifest.files.size() << "\n";
                for (const auto& f : pkg.manifest.files) {
                    out << "    - " << f.path << " (" << f.size << " bytes, sha256: "
                        << (f.sha256_hex.empty() ? "none" : f.sha256_hex.substr(0, 8) + "...") << ")\n";
                }
            }
            return (val == packaging::PackageValidationResult::Valid) ? 0 : 1;
        }

        if (cmd == "pkg-validate") {
            if (args.size() < 2) {
                err << "Usage: pkg-validate <pkg_path>\n";
                return 1;
            }
            const std::string& path = args[1];
            std::vector<uint8_t> data;
            if (!vfs_ || !vfs_->read_file(path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << path << "\n";
                return 1;
            }
            packaging::AppPackage pkg{};
            auto val = packaging::AppPackage::validate_package_data(data.data(), data.size(), &pkg);
            if (val != packaging::PackageValidationResult::Valid) {
                err << "Package validation failed: " << packaging::package_validation_result_to_string(val) << "\n";
                return 1;
            }
            auto trust = packaging::AppPackage::evaluate_trust(pkg, true);
            out << "Package is VALID:\n"
                << "  Integrity:  " << (trust.integrity_verified ? "PASSED" : "FAILED") << "\n"
                << "  Trust Level:" << (trust.trust_level == packaging::PackageTrustLevel::PlatformSigned ? "PlatformSigned" :
                                      (trust.trust_level == packaging::PackageTrustLevel::DeveloperLocal ? "DeveloperLocal" : "Unsigned")) << "\n";
            return 0;
        }

        if (cmd == "app-install") {
            if (args.size() < 2) {
                err << "Usage: app-install <pkg_path>\n";
                return 1;
            }
            const std::string& path = args[1];
            std::vector<uint8_t> data;
            if (!vfs_ || !vfs_->read_file(path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << path << "\n";
                return 1;
            }
            packaging::AppPackage pkg{};
            if (!packaging::AppPackage::unpack(data.data(), data.size(), pkg)) {
                err << "Error: Failed to unpack package\n";
                return 1;
            }
            if (!installer_) {
                err << "Error: Package installer not available\n";
                return 1;
            }
            auto status = installer_->install(pkg);
            if (status != packaging::InstallStatus::Success) {
                err << "Installation failed: " << packaging::install_status_to_string(status) << "\n";
                return 1;
            }
            out << "Successfully installed " << pkg.manifest.bundle_id << " v" << pkg.manifest.version << "\n";
            return 0;
        }

        if (cmd == "app-uninstall") {
            if (args.size() < 2) {
                err << "Usage: app-uninstall <bundle_id> [--purge]\n";
                return 1;
            }
            const std::string& bid = args[1];
            bool purge = (args.size() > 2 && args[2] == "--purge");
            if (!installer_) {
                err << "Error: Package installer not available\n";
                return 1;
            }
            auto status = installer_->uninstall(bid, purge);
            if (status != packaging::InstallStatus::Success) {
                err << "Uninstallation failed: " << packaging::install_status_to_string(status) << "\n";
                return 1;
            }
            out << "Successfully uninstalled " << bid << "\n";
            return 0;
        }

        if (cmd == "app-launch") {
            if (args.size() < 2) {
                err << "Usage: app-launch <bundle_id>\n";
                return 1;
            }
            const std::string& bid = args[1];
            if (!launcher_) {
                err << "Error: Launch pipeline not available\n";
                return 1;
            }
            auto res = launcher_->launch(bid);
            if (res.status != loader::LaunchStatus::Success) {
                err << "Launch failed: " << res.rejection_reason
                    << " (" << loader::launch_status_to_string(res.status) << ")\n";
                return 1;
            }
            out << "Launched " << bid << " (PID " << res.handle.pid
                << ", Generation " << res.handle.generation << ")\n";
            return 0;
        }

        if (cmd == "app-terminate") {
            if (args.size() < 2) {
                err << "Usage: app-terminate <pid>\n";
                return 1;
            }
            uint32_t pid = 0;
            try { pid = static_cast<uint32_t>(std::stoul(args[1])); } catch (...) {
                err << "Error: Invalid PID: " << args[1] << "\n";
                return 1;
            }
            if (!identity_mgr_) {
                err << "Error: Process identity manager not available\n";
                return 1;
            }
            uint32_t gen = identity_mgr_->get_active_generation(pid);
            kernel::ProcessHandle h{pid, gen};
            if (!launcher_ || !launcher_->terminate(h)) {
                err << "Error: Failed to terminate process " << pid << "\n";
                return 1;
            }
            out << "Terminated process " << pid << "\n";
            return 0;
        }

        if (cmd == "diag-query") {
            if (!diag_svc_) {
                err << "Error: Diagnostics service not available\n";
                return 1;
            }
            auto logs = diag_svc_->get_logs(20);
            out << "Diagnostics Logs (" << logs.size() << "):\n";
            for (const auto& l : logs) {
                out << "  [" << services::log_level_to_string(l.level) << "] "
                    << (l.bundle_id.empty() ? "System" : l.bundle_id) << " (PID " << l.pid << "): "
                    << l.message << "\n";
            }
            return 0;
        }

        err << "Unknown command: " << cmd << ". Run 'help' for usage.\n";
        return 1;
    }

private:
    void print_help(std::ostream& out) {
        out << "WinIOS Application Developer CLI\n"
            << "Usage: winios-cli <command> [options]\n\n"
            << "Commands:\n"
            << "  pkg-inspect <pkg_path>          Inspect package headers and files\n"
            << "  pkg-validate <pkg_path>         Validate package integrity and trust level\n"
            << "  app-list [--active-only]        List installed applications\n"
            << "  app-info <bundle_id>            Display detailed application metadata\n"
            << "  app-status <bundle_id>          Check runtime state of an application\n"
            << "  app-install <pkg_path>          Install application package\n"
            << "  app-uninstall <bundle_id>       Uninstall application\n"
            << "  app-launch <bundle_id>          Launch application into active session\n"
            << "  app-terminate <pid>             Terminate running process\n"
            << "  diag-query                      Query recent diagnostic logs\n"
            << "  help                            Display this help menu\n";
    }

    std::shared_ptr<vfs::VirtualFileSystem>           vfs_;
    std::shared_ptr<packaging::ApplicationRegistry>   registry_;
    std::shared_ptr<packaging::PackageInstaller>      installer_;
    std::shared_ptr<loader::AppLaunchPipeline>        launcher_;
    std::shared_ptr<kernel::ProcessIdentityManager>   identity_mgr_;
    std::shared_ptr<services::DiagnosticsService>     diag_svc_;
};

} // namespace winios::tools
