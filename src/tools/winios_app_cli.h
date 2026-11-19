#pragma once

#include "packaging/app_package.h"
#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/canonical_path_validator.h"
#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <cctype>

namespace winios::tools {

enum CliExitCode : int {
    Success = 0,
    InvalidArgs = 1,
    ExecutionFailure = 2,
    ServiceUnavailable = 3
};

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

    static bool is_valid_bundle_id(const std::string& bid) noexcept {
        if (bid.size() < 3 || bid.size() > 128) {
            return false;
        }
        bool has_dot = false;
        for (std::size_t i = 0; i < bid.size(); ++i) {
            char c = bid[i];
            if (c == '.') {
                has_dot = true;
                if (i == 0 || i == bid.size() - 1) return false;
                if (bid[i - 1] == '.') return false;
            } else if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                return false;
            }
        }
        return has_dot;
    }

    static bool parse_pid(const std::string& s, uint32_t& out_pid) noexcept {
        if (s.empty() || s.size() > 10) return false;
        for (char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        try {
            unsigned long v = std::stoul(s);
            if (v == 0 || v > 0xFFFFFFFFUL) return false;
            out_pid = static_cast<uint32_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    }

    int execute(const std::vector<std::string>& args, std::ostream& out = std::cout, std::ostream& err = std::cerr) {
        if (args.empty()) {
            print_help(out);
            return CliExitCode::Success;
        }

        const std::string& cmd = args[0];
        if (cmd == "help" || cmd == "--help" || cmd == "-h") {
            print_help(out);
            return CliExitCode::Success;
        }

        if (cmd == "app-list") {
            if (args.size() > 2) {
                err << "Usage: app-list [--active-only]\n";
                return CliExitCode::InvalidArgs;
            }
            bool active_only = false;
            if (args.size() == 2) {
                if (args[1] == "--active-only") {
                    active_only = true;
                } else {
                    err << "Error: Unknown option: " << args[1] << "\n";
                    return CliExitCode::InvalidArgs;
                }
            }
            if (!registry_) {
                err << "Error: Application registry not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            auto apps = registry_->list_apps(active_only);
            out << "Installed Applications (" << apps.size() << "):\n";
            for (const auto& a : apps) {
                out << "  " << a.bundle_id << " [" << a.name << "] v" << a.version
                    << " (State: " << packaging::installation_state_to_string(a.state)
                    << ", Active: " << (a.is_active ? "yes" : "no") << ")\n";
            }
            return CliExitCode::Success;
        }

        if (cmd == "app-info") {
            if (args.size() != 2) {
                err << "Usage: app-info <bundle_id>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& bid = args[1];
            if (!is_valid_bundle_id(bid)) {
                err << "Error: Invalid bundle identifier syntax: " << bid << "\n";
                return CliExitCode::InvalidArgs;
            }
            if (!registry_) {
                err << "Error: Application registry not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            auto a_opt = registry_->get_app(bid);
            if (!a_opt.has_value()) {
                err << "Error: Application " << bid << " not found\n";
                return CliExitCode::InvalidArgs;
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
            return CliExitCode::Success;
        }

        if (cmd == "app-status") {
            if (args.size() != 2) {
                err << "Usage: app-status <bundle_id>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& bid = args[1];
            if (!is_valid_bundle_id(bid)) {
                err << "Error: Invalid bundle identifier syntax: " << bid << "\n";
                return CliExitCode::InvalidArgs;
            }
            if (!identity_mgr_) {
                err << "Error: Process identity manager not available\n";
                return CliExitCode::ServiceUnavailable;
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
            return CliExitCode::Success;
        }

        if (cmd == "pkg-inspect" || cmd == "app-inspect") {
            if (args.size() != 2) {
                err << "Usage: " << cmd << " <pkg_path>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& raw_path = args[1];
            std::string norm_path;
            auto p_st = packaging::CanonicalPathValidator::validate_and_normalize_vfs_path(raw_path, norm_path);
            if (p_st != packaging::PathValidationStatus::Valid) {
                err << "Error: Invalid package path: " << packaging::path_validation_status_to_string(p_st) << "\n";
                return CliExitCode::InvalidArgs;
            }

            if (!vfs_) {
                err << "Error: Virtual file system not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            std::vector<uint8_t> data;
            if (!vfs_->read_file(norm_path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << norm_path << "\n";
                return CliExitCode::ExecutionFailure;
            }
            packaging::AppPackage pkg{};
            auto val = packaging::AppPackage::validate_package_data(data.data(), data.size(), &pkg);
            out << "Package Inspection: " << norm_path << " (" << data.size() << " bytes)\n"
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
                return CliExitCode::Success;
            }
            return CliExitCode::ExecutionFailure;
        }

        if (cmd == "pkg-validate") {
            if (args.size() != 2) {
                err << "Usage: pkg-validate <pkg_path>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& raw_path = args[1];
            std::string norm_path;
            auto p_st = packaging::CanonicalPathValidator::validate_and_normalize_vfs_path(raw_path, norm_path);
            if (p_st != packaging::PathValidationStatus::Valid) {
                err << "Error: Invalid package path: " << packaging::path_validation_status_to_string(p_st) << "\n";
                return CliExitCode::InvalidArgs;
            }

            if (!vfs_) {
                err << "Error: Virtual file system not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            std::vector<uint8_t> data;
            if (!vfs_->read_file(norm_path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << norm_path << "\n";
                return CliExitCode::ExecutionFailure;
            }
            packaging::AppPackage pkg{};
            auto val = packaging::AppPackage::validate_package_data(data.data(), data.size(), &pkg);
            if (val != packaging::PackageValidationResult::Valid) {
                err << "Package validation failed: " << packaging::package_validation_result_to_string(val) << "\n";
                return CliExitCode::ExecutionFailure;
            }
            auto trust = packaging::AppPackage::evaluate_trust(pkg, true);
            if (!trust.integrity_verified) {
                err << "Package integrity check failed\n";
                return CliExitCode::ExecutionFailure;
            }
            out << "Package is VALID:\n"
                << "  Integrity:  PASSED\n"
                << "  Trust Level:" << (trust.trust_level == packaging::PackageTrustLevel::PlatformSigned ? "PlatformSigned" :
                                      (trust.trust_level == packaging::PackageTrustLevel::DeveloperLocal ? "DeveloperLocal" : "Unsigned")) << "\n";
            return CliExitCode::Success;
        }

        if (cmd == "app-install") {
            if (args.size() != 2) {
                err << "Usage: app-install <pkg_path>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& raw_path = args[1];
            std::string norm_path;
            auto p_st = packaging::CanonicalPathValidator::validate_and_normalize_vfs_path(raw_path, norm_path);
            if (p_st != packaging::PathValidationStatus::Valid) {
                err << "Error: Invalid package path: " << packaging::path_validation_status_to_string(p_st) << "\n";
                return CliExitCode::InvalidArgs;
            }

            if (!vfs_ || !installer_) {
                err << "Error: Package installer or VFS not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            std::vector<uint8_t> data;
            if (!vfs_->read_file(norm_path, data) || data.empty()) {
                err << "Error: Failed to read package file: " << norm_path << "\n";
                return CliExitCode::ExecutionFailure;
            }
            packaging::AppPackage pkg{};
            if (!packaging::AppPackage::unpack(data.data(), data.size(), pkg)) {
                err << "Error: Failed to unpack package\n";
                return CliExitCode::ExecutionFailure;
            }
            auto status = installer_->install(pkg);
            if (status != packaging::InstallStatus::Success) {
                err << "Installation failed: " << packaging::install_status_to_string(status) << "\n";
                return CliExitCode::ExecutionFailure;
            }
            out << "Successfully installed " << pkg.manifest.bundle_id << " v" << pkg.manifest.version << "\n";
            return CliExitCode::Success;
        }

        if (cmd == "app-uninstall") {
            if (args.size() < 2 || args.size() > 3) {
                err << "Usage: app-uninstall <bundle_id> [--purge]\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& bid = args[1];
            if (!is_valid_bundle_id(bid)) {
                err << "Error: Invalid bundle identifier syntax: " << bid << "\n";
                return CliExitCode::InvalidArgs;
            }
            bool purge = false;
            if (args.size() == 3) {
                if (args[2] == "--purge") {
                    purge = true;
                } else {
                    err << "Error: Unknown option: " << args[2] << "\n";
                    return CliExitCode::InvalidArgs;
                }
            }
            if (!installer_) {
                err << "Error: Package installer not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            auto status = installer_->uninstall(bid, purge);
            if (status != packaging::InstallStatus::Success) {
                err << "Uninstallation failed: " << packaging::install_status_to_string(status) << "\n";
                return CliExitCode::ExecutionFailure;
            }
            out << "Successfully uninstalled " << bid << "\n";
            return CliExitCode::Success;
        }

        if (cmd == "app-launch") {
            if (args.size() != 2) {
                err << "Usage: app-launch <bundle_id>\n";
                return CliExitCode::InvalidArgs;
            }
            const std::string& bid = args[1];
            if (!is_valid_bundle_id(bid)) {
                err << "Error: Invalid bundle identifier syntax: " << bid << "\n";
                return CliExitCode::InvalidArgs;
            }
            if (!launcher_) {
                err << "Error: Launch pipeline not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            auto res = launcher_->launch(bid);
            if (res.status != loader::LaunchStatus::Success) {
                err << "Launch failed: " << res.rejection_reason
                    << " (" << loader::launch_status_to_string(res.status) << ")\n";
                return CliExitCode::ExecutionFailure;
            }
            out << "Launched " << bid << " (PID " << res.handle.pid
                << ", Generation " << res.handle.generation << ")\n";
            return CliExitCode::Success;
        }

        if (cmd == "app-terminate") {
            if (args.size() != 2) {
                err << "Usage: app-terminate <pid>\n";
                return CliExitCode::InvalidArgs;
            }
            uint32_t pid = 0;
            if (!parse_pid(args[1], pid)) {
                err << "Error: Invalid PID: " << args[1] << "\n";
                return CliExitCode::InvalidArgs;
            }
            if (!identity_mgr_ || !launcher_) {
                err << "Error: Process identity manager or launcher not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            uint32_t gen = identity_mgr_->get_active_generation(pid);
            if (gen == 0) {
                err << "Error: No active process found with PID " << pid << "\n";
                return CliExitCode::ExecutionFailure;
            }
            kernel::ProcessHandle h{pid, gen};
            if (!launcher_->terminate(h)) {
                err << "Error: Failed to terminate process " << pid << "\n";
                return CliExitCode::ExecutionFailure;
            }
            out << "Terminated process " << pid << "\n";
            return CliExitCode::Success;
        }

        if (cmd == "diag-query") {
            size_t max_logs = 20;
            if (args.size() == 3) {
                if (args[1] != "--limit") {
                    err << "Usage: diag-query [--limit <count>]\n";
                    return CliExitCode::InvalidArgs;
                }
                uint32_t lim = 0;
                if (!parse_pid(args[2], lim) || lim == 0 || lim > 1000) {
                    err << "Error: Invalid limit value: " << args[2] << " (must be 1-1000)\n";
                    return CliExitCode::InvalidArgs;
                }
                max_logs = lim;
            } else if (args.size() != 1) {
                err << "Usage: diag-query [--limit <count>]\n";
                return CliExitCode::InvalidArgs;
            }

            if (!diag_svc_) {
                err << "Error: Diagnostics service not available\n";
                return CliExitCode::ServiceUnavailable;
            }
            auto logs = diag_svc_->get_logs(max_logs);
            out << "Diagnostics Logs (" << logs.size() << "):\n";
            for (const auto& l : logs) {
                out << "  [" << services::log_level_to_string(l.level) << "] "
                    << (l.bundle_id.empty() ? "System" : l.bundle_id) << " (PID " << l.pid << "): "
                    << l.message << "\n";
            }
            return CliExitCode::Success;
        }

        err << "Unknown command: " << cmd << ". Run 'help' for usage.\n";
        return CliExitCode::InvalidArgs;
    }

private:
    void print_help(std::ostream& out) {
        out << "WinIOS Application Developer CLI\n"
            << "Usage: winios-cli <command> [options]\n\n"
            << "Commands:\n"
            << "  pkg-inspect <pkg_path>          Inspect package headers and files\n"
            << "  app-inspect <pkg_path>          Alias for pkg-inspect\n"
            << "  pkg-validate <pkg_path>         Validate package integrity and trust level\n"
            << "  app-list [--active-only]        List installed applications\n"
            << "  app-info <bundle_id>            Display detailed application metadata\n"
            << "  app-status <bundle_id>          Check runtime state of an application\n"
            << "  app-install <pkg_path>          Install application package\n"
            << "  app-uninstall <bundle_id>       Uninstall application [--purge]\n"
            << "  app-launch <bundle_id>          Launch application into active session\n"
            << "  app-terminate <pid>             Terminate running process\n"
            << "  diag-query [--limit <count>]    Query recent diagnostic logs\n"
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
