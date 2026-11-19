#pragma once

#include "packaging/app_package.h"
#include "packaging/package_installer.h"
#include "packaging/app_registry.h"
#include "packaging/canonical_path_validator.h"
#include "loader/app_launcher.h"
#include "kernel/process_identity.h"
#include "services/diagnostics_service.h"
#include "security/developer_keystore.h"
#include "security/publisher_trust_store.h"
#include "vfs/vfs.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <filesystem>
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
    WiniosAppCli() = default;

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

    static std::optional<std::string> get_opt(const std::vector<std::string>& args, const std::string& flag) {
        for (std::size_t i = 0; i + 1 < args.size(); ++i) {
            if (args[i] == flag) return args[i + 1];
        }
        return std::nullopt;
    }

    static bool has_flag(const std::vector<std::string>& args, const std::string& flag) {
        for (const auto& a : args) {
            if (a == flag) return true;
        }
        return false;
    }

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

        if (cmd == "keygen") {
            auto pub_id = get_opt(args, "--publisher-id");
            auto out_file = get_opt(args, "--out");
            if (!pub_id || !out_file) {
                err << "Usage: keygen --publisher-id <id> --out <file> [--org <name>] [--key-id <id>] [--passphrase <pass>] [--protection dpapi|passphrase|none] [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            std::string org = get_opt(args, "--org").value_or("WinIOS Developer");
            std::string key_id = get_opt(args, "--key-id").value_or(
                "KEY-DEV-" + *pub_id + "-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
            std::string pass = get_opt(args, "--passphrase").value_or("");
            std::string prot_str = get_opt(args, "--protection").value_or("dpapi");

            security::KeystoreProtection prot = security::KeystoreProtection::DPAPI;
            if (prot_str == "passphrase") prot = security::KeystoreProtection::Passphrase;
            else if (prot_str == "none") prot = security::KeystoreProtection::None;
            else if (prot_str == "dpapi") prot = security::KeystoreProtection::DPAPI;

            auto id = security::DeveloperKeyStore::generate_identity(*pub_id, org, key_id);
            if (!security::DeveloperKeyStore::save_to_file(*out_file, id, prot, pass)) {
                err << "Error: Failed to save keystore to " << *out_file << "\n";
                return CliExitCode::ExecutionFailure;
            }

            if (has_flag(args, "--json")) {
                out << "{\"status\":\"ok\",\"publisher_id\":\"" << *pub_id
                    << "\",\"key_id\":\"" << key_id
                    << "\",\"out\":\"" << *out_file << "\"}\n";
            } else {
                out << "Generated developer identity: " << *pub_id << " (" << key_id << ")\n";
                out << "Saved to: " << *out_file << "\n";
            }
            return CliExitCode::Success;
        }

        if (cmd == "key-export-pub") {
            auto key_file = get_opt(args, "--key");
            auto out_file = get_opt(args, "--out");
            if (!key_file || !out_file) {
                err << "Usage: key-export-pub --key <file> --out <file> [--passphrase <pass>] [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            std::string pass = get_opt(args, "--passphrase").value_or("");
            auto maybe_id = security::DeveloperKeyStore::load_from_file(*key_file, pass);
            if (!maybe_id.has_value()) {
                err << "Error: Failed to load keystore from " << *key_file << " (invalid passphrase or corrupted)\n";
                return CliExitCode::ExecutionFailure;
            }
            if (!security::DeveloperKeyStore::save_public_key_file(*out_file, *maybe_id)) {
                err << "Error: Failed to write public key to " << *out_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            if (has_flag(args, "--json")) {
                out << "{\"status\":\"ok\",\"key_id\":\"" << maybe_id->key_id
                    << "\",\"out\":\"" << *out_file << "\"}\n";
            } else {
                out << "Exported public key for " << maybe_id->key_id << " to " << *out_file << "\n";
            }
            return CliExitCode::Success;
        }

        if (cmd == "app-sign") {
            auto pkg_file = get_opt(args, "--pkg");
            auto key_file = get_opt(args, "--key");
            if (!pkg_file || !key_file) {
                err << "Usage: app-sign --pkg <file> --key <file> [--out <file>] [--passphrase <pass>] [--valid-days <n>] [--flags <f>] [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            std::string out_file = get_opt(args, "--out").value_or(*pkg_file);
            std::string pass = get_opt(args, "--passphrase").value_or("");
            uint64_t valid_days = 365;
            if (auto days_opt = get_opt(args, "--valid-days")) {
                try { valid_days = std::stoull(*days_opt); } catch (...) {}
            }
            uint32_t flags = 0;
            if (auto flags_opt = get_opt(args, "--flags")) {
                try { flags = static_cast<uint32_t>(std::stoul(*flags_opt)); } catch (...) {}
            }

            std::ifstream ifs(*pkg_file, std::ios::binary);
            if (!ifs.is_open()) {
                err << "Error: Cannot open package file: " << *pkg_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            std::vector<std::uint8_t> pkg_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            ifs.close();

            auto maybe_id = security::DeveloperKeyStore::load_from_file(*key_file, pass);
            if (!maybe_id.has_value()) {
                err << "Error: Cannot load developer key: " << *key_file << "\n";
                return CliExitCode::ExecutionFailure;
            }

            uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            uint64_t valid_until = (valid_days > 0) ? (now + valid_days * 86400) : 0;

            if (!security::PublisherTrustStore::sign_package_v2(pkg_data, *maybe_id, flags, now, valid_until)) {
                err << "Error: Signing failed\n";
                return CliExitCode::ExecutionFailure;
            }

            std::ofstream ofs(out_file, std::ios::binary);
            if (!ofs.is_open()) {
                err << "Error: Cannot write signed package to: " << out_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            ofs.write(reinterpret_cast<const char*>(pkg_data.data()), static_cast<std::streamsize>(pkg_data.size()));
            ofs.close();

            if (has_flag(args, "--json")) {
                out << "{\"status\":\"ok\",\"package\":\"" << out_file
                    << "\",\"publisher_id\":\"" << maybe_id->publisher_id
                    << "\",\"key_id\":\"" << maybe_id->key_id
                    << "\",\"trailer_version\":2}\n";
            } else {
                out << "Signed " << out_file << " using key " << maybe_id->key_id << " (WIOSAUTH v2)\n";
            }
            return CliExitCode::Success;
        }

        if (cmd == "app-verify") {
            auto pkg_file = get_opt(args, "--pkg");
            if (!pkg_file) {
                err << "Usage: app-verify --pkg <file> [--trust-store <file>] [--policy dev|prod] [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            security::PublisherTrustStore store;
            if (auto ts_file = get_opt(args, "--trust-store")) {
                std::ifstream ifs(*ts_file);
                if (!ifs.is_open()) {
                    err << "Error: Cannot open trust store: " << *ts_file << "\n";
                    return CliExitCode::ExecutionFailure;
                }
                std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                if (!store.deserialize(text)) {
                    err << "Error: Failed to parse trust store file: " << *ts_file << "\n";
                    return CliExitCode::ExecutionFailure;
                }
            } else {
                store = security::PublisherTrustStore::create_default_test_store();
            }

            security::TrustPolicy policy = security::TrustPolicy::Development;
            if (auto pol_opt = get_opt(args, "--policy")) {
                if (*pol_opt == "prod" || *pol_opt == "StrictProduction") {
                    policy = security::TrustPolicy::StrictProduction;
                }
            }

            std::ifstream ifs(*pkg_file, std::ios::binary);
            if (!ifs.is_open()) {
                err << "Error: Cannot open package: " << *pkg_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            std::vector<std::uint8_t> pkg_data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            ifs.close();

            uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            auto res = store.verify_package_signature(pkg_data.data(), pkg_data.size(), policy, now);

            if (has_flag(args, "--json")) {
                out << "{\"status\":\"" << security::signature_status_to_string(res.status)
                    << "\",\"trusted\":" << (res.is_trusted ? "true" : "false")
                    << ",\"publisher_id\":\"" << res.publisher_id
                    << "\",\"key_id\":\"" << res.key_id
                    << "\",\"trailer_version\":" << res.trailer_version
                    << ",\"trailer_flags\":" << res.trailer_flags
                    << ",\"failure_reason\":\"" << res.failure_reason << "\"}\n";
            } else {
                out << "Package Verification: " << *pkg_file << "\n";
                out << "  Status: " << security::signature_status_to_string(res.status) << "\n";
                out << "  Trusted: " << (res.is_trusted ? "YES" : "NO") << "\n";
                out << "  Publisher: " << (res.publisher_id.empty() ? "(none)" : res.publisher_id) << "\n";
                out << "  Key ID: " << (res.key_id.empty() ? "(none)" : res.key_id) << "\n";
                out << "  Trailer Version: " << res.trailer_version << "\n";
                if (!res.is_trusted && !res.failure_reason.empty()) {
                    out << "  Failure Reason: " << res.failure_reason << "\n";
                }
            }

            return res.is_trusted ? CliExitCode::Success : CliExitCode::ExecutionFailure;
        }

        if (cmd == "trust-add-key") {
            auto ts_file = get_opt(args, "--trust-store");
            auto key_file = get_opt(args, "--key-file");
            if (!ts_file || !key_file) {
                err << "Usage: trust-add-key --trust-store <file> --key-file <file> [--root] [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            security::PublisherTrustStore store;
            if (std::filesystem::exists(*ts_file)) {
                std::ifstream ifs(*ts_file);
                std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                store.deserialize(text);
            }
            std::ifstream kfs(*key_file);
            if (!kfs.is_open()) {
                err << "Error: Cannot open key file: " << *key_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            std::string key_text((std::istreambuf_iterator<char>(kfs)), std::istreambuf_iterator<char>());
            security::PublisherTrustStore temp;
            if (!temp.deserialize(key_text) || temp.total_keys() == 0) {
                err << "Error: Failed to parse key file: " << *key_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            bool is_root = has_flag(args, "--root");
            for (const auto& [_, k] : temp.all_keys()) {
                if (is_root) {
                    store.register_root_anchor(k);
                } else {
                    store.register_key(k);
                }
            }
            std::ofstream ofs(*ts_file);
            if (!ofs.is_open()) {
                err << "Error: Cannot write trust store to " << *ts_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            ofs << store.serialize();
            if (has_flag(args, "--json")) {
                out << "{\"status\":\"ok\",\"trust_store\":\"" << *ts_file << "\",\"total_keys\":" << store.total_keys() << "}\n";
            } else {
                out << "Added key(s) to trust store: " << *ts_file << " (total keys: " << store.total_keys() << ")\n";
            }
            return CliExitCode::Success;
        }

        if (cmd == "trust-revoke") {
            auto ts_file = get_opt(args, "--trust-store");
            auto key_id = get_opt(args, "--key-id");
            auto pub_id = get_opt(args, "--publisher-id");
            if (!ts_file || (!key_id && !pub_id)) {
                err << "Usage: trust-revoke --trust-store <file> (--key-id <id> | --publisher-id <id>) [--json]\n";
                return CliExitCode::InvalidArgs;
            }
            security::PublisherTrustStore store;
            if (std::filesystem::exists(*ts_file)) {
                std::ifstream ifs(*ts_file);
                std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                store.deserialize(text);
            }
            if (key_id) store.revoke_key(*key_id);
            if (pub_id) store.revoke_publisher(*pub_id);

            std::ofstream ofs(*ts_file);
            if (!ofs.is_open()) {
                err << "Error: Cannot write trust store to " << *ts_file << "\n";
                return CliExitCode::ExecutionFailure;
            }
            ofs << store.serialize();
            if (has_flag(args, "--json")) {
                out << "{\"status\":\"ok\",\"revoked\":\"" << (key_id ? *key_id : *pub_id) << "\"}\n";
            } else {
                out << "Revoked: " << (key_id ? *key_id : *pub_id) << " in " << *ts_file << "\n";
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
            << "  keygen [options]                Generate developer signing keypair\n"
            << "  key-export-pub [options]        Export public verification key\n"
            << "  app-sign [options]              Sign application package with key\n"
            << "  app-verify [options]            Verify package authenticity & trust\n"
            << "  trust-add-key [options]         Import public key into trust store\n"
            << "  trust-revoke [options]          Revoke key or publisher in trust store\n"
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
