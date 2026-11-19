#include "packaging/package_installer.h"
#include "packaging/canonical_path_validator.h"
#include "security/security_utils.h"
#include "crypto/sha256.h"

#include <chrono>
#include <sstream>
#include <vector>

namespace winios::packaging {

namespace {

static void remove_tree(vfs::VirtualFileSystem& vfs, const std::string& path) {
    if (!vfs.exists(path)) return;
    std::vector<vfs::VfsDirEntry> entries;
    if (vfs.readdir(path, entries)) {
        for (const auto& e : entries) {
            if (e.name == "." || e.name == "..") continue;
            std::string sub = path + "/" + e.name;
            if (e.type == 4) { // DT_DIR
                remove_tree(vfs, sub);
            } else {
                vfs.unlink(sub);
            }
        }
    }
    vfs.rmdir(path);
}

} // namespace

int PackageInstaller::compare_versions(const std::string& v1, const std::string& v2) noexcept {
    auto parse_semver = [](const std::string& s) -> std::vector<int> {
        std::vector<int> parts;
        std::istringstream ss(s);
        std::string token;
        while (std::getline(ss, token, '.')) {
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                parts.push_back(0);
            }
        }
        while (parts.size() < 3) parts.push_back(0);
        return parts;
    };

    const auto p1 = parse_semver(v1);
    const auto p2 = parse_semver(v2);
    for (std::size_t i = 0; i < 3; ++i) {
        if (p1[i] < p2[i]) return -1;
        if (p1[i] > p2[i]) return 1;
    }
    return 0;
}

PackageInstaller::PackageInstaller(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                                   std::shared_ptr<ApplicationRegistry> registry)
    : vfs_(std::move(vfs))
    , registry_(std::move(registry)) {
    if (vfs_) {
        if (!vfs_->exists("/Applications")) vfs_->mkdir("/Applications");
        if (!vfs_->exists("/staging")) vfs_->mkdir("/staging");
        if (!vfs_->exists("/backup")) vfs_->mkdir("/backup");
    }
}

void PackageInstaller::rollback_staging(const std::string& staging_dir) {
    if (!vfs_ || !vfs_->exists(staging_dir)) return;
    remove_tree(*vfs_, staging_dir);
}

bool PackageInstaller::copy_directory(const std::string& src_dir, const std::string& dst_dir) {
    if (!vfs_ || !vfs_->exists(src_dir)) return false;
    if (!vfs_->exists(dst_dir) && !vfs_->mkdir(dst_dir)) return false;

    std::vector<vfs::VfsDirEntry> entries;
    if (!vfs_->readdir(src_dir, entries)) return false;

    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;
        const std::string src_path = src_dir + "/" + e.name;
        const std::string dst_path = dst_dir + "/" + e.name;
        if (e.type == 4) { // DT_DIR
            if (!copy_directory(src_path, dst_path)) return false;
        } else {
            std::vector<std::uint8_t> data;
            if (!vfs_->read_file(src_path, data)) return false;
            if (!vfs_->create_file(dst_path)) return false;
            if (!data.empty() && !vfs_->write_file(dst_path, data.data(), data.size())) return false;
        }
    }
    return true;
}

bool PackageInstaller::write_journal(const std::string& tx_id,
                                     const std::string& bundle_id,
                                     TransactionState state,
                                     bool is_update,
                                     const std::string& target_dir,
                                     const std::string& backup_dir) {
    if (!vfs_) return false;
    const std::string staging_tx = "/staging/" + tx_id;
    if (!vfs_->exists(staging_tx)) {
        vfs_->mkdir(staging_tx);
    }
    const std::string journal_path = staging_tx + "/journal.json";
    const std::string state_str = installer_state_to_string(state);

    const std::string digest_input = tx_id + "|" + bundle_id + "|" + state_str + "|" +
                                     (is_update ? "1" : "0") + "|" + target_dir + "|" + backup_dir;
    const std::string chk = crypto::Sha256::hash_hex(
        reinterpret_cast<const std::uint8_t*>(digest_input.data()), digest_input.size());

    std::ostringstream ss;
    ss << "{\n"
       << "  \"tx_id\": \"" << tx_id << "\",\n"
       << "  \"bundle_id\": \"" << bundle_id << "\",\n"
       << "  \"state\": \"" << state_str << "\",\n"
       << "  \"is_update\": " << (is_update ? "true" : "false") << ",\n"
       << "  \"target_dir\": \"" << target_dir << "\",\n"
       << "  \"backup_dir\": \"" << backup_dir << "\",\n"
       << "  \"checksum\": \"" << chk << "\"\n"
       << "}\n";

    std::string data = ss.str();
    if (!vfs_->exists(journal_path)) {
        vfs_->create_file(journal_path);
    }
    return vfs_->write_file(journal_path, data.data(), data.size());
}

InstallStatus PackageInstaller::install(const AppPackage& pkg) {
    if (!vfs_ || !registry_) return InstallStatus::VfsError;

    // Allocate unique transaction ID
    const std::string tx_id = "tx_" + security::SecureRandom::get_random_hex(8);
    const std::string& bundle_id = pkg.manifest.bundle_id;
    const std::string target_dir = "/Applications/" + bundle_id + ".app";
    const std::string staging_tx = "/staging/" + tx_id;
    const std::string stage_bundle_dir = staging_tx + "/bundle";

    if (!vfs_->exists("/staging")) vfs_->mkdir("/staging");
    if (!vfs_->mkdir(staging_tx)) return InstallStatus::StagingFailed;

    auto do_transition = [&](InstallerState state) -> bool {
        write_journal(tx_id, bundle_id, state, false, target_dir, "");
        if (fault_hook_) fault_hook_(state);
        if (inject_fault_state_.has_value() && inject_fault_state_.value() == state) {
            return false;
        }
        return true;
    };

    // 1. Transition: Initiated
    if (!do_transition(InstallerState::Initiated)) {
        return InstallStatus::CommitFailed;
    }

    // 2. Transition: Validating
    if (!do_transition(InstallerState::Validating)) {
        return InstallStatus::CommitFailed;
    }

    // Validate manifest
    if (!pkg.manifest.is_valid()) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::InvalidManifest;
    }

    // Check if already installed
    if (registry_->is_installed(bundle_id)) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::AlreadyInstalled;
    }

    // 3. Transition: ManifestParsed
    if (!do_transition(InstallerState::ManifestParsed)) {
        return InstallStatus::CommitFailed;
    }

    // Validate canonical path for every file in package
    std::vector<std::pair<std::string, const FileEntry*>> normalized_files;
    for (const auto& f : pkg.manifest.files) {
        std::string norm_path;
        if (CanonicalPathValidator::validate_and_normalize_bundle_path(f.path, norm_path) != PathValidationStatus::Valid) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
            rollback_staging(staging_tx);
            return InstallStatus::InvalidManifest;
        }
        normalized_files.push_back({norm_path, &f});
    }

    // 4. Transition: SecurityVerified
    if (!do_transition(InstallerState::SecurityVerified)) {
        return InstallStatus::CommitFailed;
    }

    // 5. Transition: Staged
    if (!do_transition(InstallerState::Staged)) {
        return InstallStatus::CommitFailed;
    }

    if (!vfs_->mkdir(stage_bundle_dir)) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::StagingFailed;
    }

    if (injected_failure_ == InjectedFailureStage::StagingWrite) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::StagingFailed;
    }

    // 6. Transition: Extracting
    if (!do_transition(InstallerState::Extracting)) {
        return InstallStatus::CommitFailed;
    }

    for (const auto& [norm_path, f_ptr] : normalized_files) {
        const std::string stage_file_path = stage_bundle_dir + "/" + norm_path;
        const auto slash_pos = norm_path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            const std::string sub = stage_bundle_dir + "/" + norm_path.substr(0, slash_pos);
            if (!vfs_->exists(sub)) {
                // Ensure all parents are created
                std::string parent_builder = stage_bundle_dir;
                std::istringstream pss(norm_path.substr(0, slash_pos));
                std::string seg;
                while (std::getline(pss, seg, '/')) {
                    if (seg.empty()) continue;
                    parent_builder += "/" + seg;
                    if (!vfs_->exists(parent_builder)) vfs_->mkdir(parent_builder);
                }
            }
        }

        if (!vfs_->create_file(stage_file_path) ||
            (f_ptr->size > 0 && !vfs_->write_file(stage_file_path, f_ptr->data.data(), f_ptr->data.size()))) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
            rollback_staging(staging_tx);
            return InstallStatus::StagingFailed;
        }
    }

    // 7. Transition: Extracted
    if (!do_transition(InstallerState::Extracted)) {
        return InstallStatus::CommitFailed;
    }

    // Pre-commit integrity check: re-read staged files and verify digests
    if (injected_failure_ == InjectedFailureStage::PreCommitIntegrity) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::IntegrityCheckFailed;
    }

    for (const auto& [norm_path, f_ptr] : normalized_files) {
        const std::string stage_file_path = stage_bundle_dir + "/" + norm_path;
        std::vector<std::uint8_t> read_back;
        if (!vfs_->read_file(stage_file_path, read_back)) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
            rollback_staging(staging_tx);
            return InstallStatus::IntegrityCheckFailed;
        }

        const std::string computed_hash = crypto::Sha256::hash_hex(read_back);
        if (!f_ptr->sha256_hex.empty() && !security::constant_time_equals(computed_hash, f_ptr->sha256_hex)) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
            rollback_staging(staging_tx);
            return InstallStatus::IntegrityCheckFailed;
        }
    }

    // 8. Transition: Registering (atomic commit phase)
    if (!do_transition(InstallerState::Registering)) {
        return InstallStatus::CommitFailed;
    }

    if (injected_failure_ == InjectedFailureStage::CommitMove) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    if (vfs_->exists(target_dir)) {
        remove_tree(*vfs_, target_dir);
    }

    if (!vfs_->rename(stage_bundle_dir, target_dir)) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    if (injected_failure_ == InjectedFailureStage::RegistryCommit) {
        remove_tree(*vfs_, target_dir);
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    // Register in ApplicationRegistry
    AppRegistration reg{};
    reg.bundle_id = bundle_id;
    reg.name = pkg.manifest.name;
    reg.display_name = pkg.manifest.display_name.empty() ? pkg.manifest.name : pkg.manifest.display_name;
    reg.version = pkg.manifest.version;
    reg.package_version = pkg.manifest.format_version;
    reg.min_sdk_version = pkg.manifest.min_sdk_version_u32;
    reg.target_abi = pkg.manifest.target_abi_version;
    reg.exec_path = pkg.manifest.executable_name;
    reg.install_dir = target_dir;
    reg.icon_path = pkg.manifest.icon_path;
    reg.startup_config = pkg.manifest.startup_config;
    reg.storage_quota_bytes = pkg.manifest.storage_quota_bytes;
    reg.permissions = pkg.manifest.permissions;
    reg.optional_permissions = pkg.manifest.optional_permissions;
    reg.supported_doc_types = pkg.manifest.supported_doc_types;
    reg.install_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    reg.update_time_us = reg.install_time_us;
    reg.is_active = true;

    if (!registry_->register_app(reg)) {
        remove_tree(*vfs_, target_dir);
        write_journal(tx_id, bundle_id, InstallerState::Aborted, false, target_dir, "");
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    for (const auto& dt : pkg.manifest.supported_doc_types) {
        registry_->set_default_app_for_type(dt, bundle_id);
    }

    // 9. Transition: Committed
    if (!do_transition(InstallerState::Committed)) {
        return InstallStatus::CommitFailed;
    }

    rollback_staging(staging_tx);
    return InstallStatus::Success;
}

namespace {

struct JournalEntry {
    std::string tx_id;
    std::string bundle_id;
    std::string state;
    bool is_update{false};
    std::string target_dir;
    std::string backup_dir;
    std::string checksum;
    bool checksum_valid{false};
};

static bool parse_journal(const std::string& json_text, JournalEntry& out) {
    auto extract = [&](const std::string& key) -> std::string {
        std::string pattern = "\"" + key + "\": \"";
        auto pos = json_text.find(pattern);
        if (pos == std::string::npos) return "";
        pos += pattern.size();
        auto end = json_text.find('"', pos);
        if (end == std::string::npos) return "";
        return json_text.substr(pos, end - pos);
    };
    out.tx_id = extract("tx_id");
    out.bundle_id = extract("bundle_id");
    out.state = extract("state");
    out.target_dir = extract("target_dir");
    out.backup_dir = extract("backup_dir");
    out.checksum = extract("checksum");
    out.is_update = (json_text.find("\"is_update\": true") != std::string::npos);

    if (out.tx_id.empty() || out.state.empty()) return false;

    if (!out.checksum.empty()) {
        const std::string digest_input = out.tx_id + "|" + out.bundle_id + "|" + out.state + "|" +
                                         (out.is_update ? "1" : "0") + "|" + out.target_dir + "|" + out.backup_dir;
        const std::string expected_chk = crypto::Sha256::hash_hex(
            reinterpret_cast<const std::uint8_t*>(digest_input.data()), digest_input.size());
        out.checksum_valid = security::constant_time_equals(out.checksum, expected_chk);
    } else {
        out.checksum_valid = true;
    }

    return true;
}

} // namespace

PackageInstaller::RecoveryReport PackageInstaller::reconcile_and_recover() {
    RecoveryReport report{};
    if (!vfs_ || !registry_) return report;

    // 1. Scan /staging
    if (vfs_->exists("/staging")) {
        std::vector<vfs::VfsDirEntry> entries;
        if (vfs_->readdir("/staging", entries)) {
            for (const auto& e : entries) {
                if (e.name == "." || e.name == "..") continue;
                const std::string staging_tx = "/staging/" + e.name;
                const std::string journal_path = staging_tx + "/journal.json";

                std::vector<std::uint8_t> journal_data;
                JournalEntry j{};
                bool has_journal = vfs_->read_file(journal_path, journal_data) &&
                                   parse_journal(std::string(journal_data.begin(), journal_data.end()), j);

                if (has_journal && !j.checksum_valid) {
                    // Journal integrity failed - handle as corrupted/abandoned staging
                    if (j.is_update && !j.backup_dir.empty() && vfs_->exists(j.backup_dir)) {
                        if (!vfs_->exists(j.target_dir)) {
                            vfs_->rename(j.backup_dir, j.target_dir);
                            report.backups_restored++;
                        } else {
                            remove_tree(*vfs_, j.backup_dir);
                        }
                    }
                    rollback_staging(staging_tx);
                    report.abandoned_transactions_cleaned++;
                    continue;
                }

                if (has_journal && (j.state == "Committed" || j.state == "Committing")) {
                    bool target_has_content = false;
                    std::vector<vfs::VfsDirEntry> target_entries;
                    if (vfs_->exists(j.target_dir) && vfs_->readdir(j.target_dir, target_entries)) {
                        for (const auto& te : target_entries) {
                            if (te.name != "." && te.name != "..") {
                                target_has_content = true;
                                break;
                            }
                        }
                    }

                    if (target_has_content) {
                        if (!registry_->is_installed(j.bundle_id)) {
                            AppRegistration reg{};
                            reg.bundle_id = j.bundle_id;
                            reg.name = j.bundle_id;
                            reg.install_dir = j.target_dir;
                            reg.version = "1.0.0";
                            reg.exec_path = "bin/main";
                            reg.is_active = true;
                            reg.state = InstallationState::Installed;
                            registry_->register_app(reg);
                        }
                        if (!j.backup_dir.empty() && vfs_->exists(j.backup_dir)) {
                            remove_tree(*vfs_, j.backup_dir);
                        }
                        rollback_staging(staging_tx);
                        report.committed_transactions_finalized++;
                    } else {
                        if (j.is_update && !j.backup_dir.empty() && vfs_->exists(j.backup_dir)) {
                            if (vfs_->exists(j.target_dir)) remove_tree(*vfs_, j.target_dir);
                            vfs_->rename(j.backup_dir, j.target_dir);
                            report.backups_restored++;
                        }
                        rollback_staging(staging_tx);
                        report.abandoned_transactions_cleaned++;
                    }
                } else {
                    if (has_journal && j.is_update && !j.backup_dir.empty() && vfs_->exists(j.backup_dir)) {
                        if (!vfs_->exists(j.target_dir)) {
                            vfs_->rename(j.backup_dir, j.target_dir);
                            report.backups_restored++;
                        } else {
                            remove_tree(*vfs_, j.backup_dir);
                        }
                    } else if (has_journal && !j.is_update) {
                        if (vfs_->exists(j.target_dir)) {
                            remove_tree(*vfs_, j.target_dir);
                        }
                        if (registry_->is_installed(j.bundle_id)) {
                            registry_->unregister_app(j.bundle_id);
                        }
                    }
                    rollback_staging(staging_tx);
                    report.abandoned_transactions_cleaned++;
                }
            }
        }
    }

    // 2. Scan leftover /backup directories
    if (vfs_->exists("/backup")) {
        std::vector<vfs::VfsDirEntry> backup_entries;
        if (vfs_->readdir("/backup", backup_entries)) {
            for (const auto& be : backup_entries) {
                if (be.name == "." || be.name == "..") continue;
                remove_tree(*vfs_, "/backup/" + be.name);
            }
        }
    }

    // 3. Reconcile Registry against /Applications
    auto registered_apps = registry_->list_apps(false);
    for (const auto& app : registered_apps) {
        if (!vfs_->exists(app.install_dir)) {
            registry_->set_installation_state(app.bundle_id, InstallationState::Corrupted);
            registry_->set_active(app.bundle_id, false);
            report.corrupted_apps_flagged++;
        }
    }

    if (vfs_->exists("/Applications")) {
        std::vector<vfs::VfsDirEntry> app_entries;
        if (vfs_->readdir("/Applications", app_entries)) {
            for (const auto& ae : app_entries) {
                if (ae.name == "." || ae.name == "..") continue;
                if (ae.type == 4 && ae.name.size() > 4 && ae.name.substr(ae.name.size() - 4) == ".app") {
                    std::string bundle_id = ae.name.substr(0, ae.name.size() - 4);
                    if (!registry_->is_installed(bundle_id)) {
                        std::vector<vfs::VfsDirEntry> sub;
                        std::string bundle_path = "/Applications/" + ae.name;
                        if (vfs_->readdir(bundle_path, sub) && sub.size() > 2) {
                            AppRegistration discovered{};
                            discovered.bundle_id = bundle_id;
                            discovered.name = bundle_id;
                            discovered.display_name = bundle_id;
                            discovered.version = "1.0.0";
                            discovered.exec_path = "bin/main";
                            discovered.install_dir = bundle_path;
                            discovered.is_active = true;
                            discovered.state = InstallationState::Installed;
                            registry_->register_app(discovered);
                            report.unindexed_apps_discovered++;
                        }
                    }
                }
            }
        }
    }

    return report;
}

std::size_t PackageInstaller::recover_interrupted_installations() {
    return reconcile_and_recover().total_recovered();
}

InstallStatus PackageInstaller::update(const AppPackage& pkg, bool allow_downgrade) {
    if (!vfs_ || !registry_) return InstallStatus::VfsError;

    if (!pkg.manifest.is_valid()) {
        return InstallStatus::InvalidManifest;
    }

    const std::string& bundle_id = pkg.manifest.bundle_id;
    auto existing_opt = registry_->get_app(bundle_id);
    if (!existing_opt.has_value()) {
        return InstallStatus::NotInstalled;
    }

    // Check version
    if (!allow_downgrade && compare_versions(pkg.manifest.version, existing_opt->version) < 0) {
        return InstallStatus::VersionDowngradeRejected;
    }

    // Check if app is currently executing
    if (is_running_cb_ && is_running_cb_(bundle_id)) {
        if (!terminate_cb_ || !terminate_cb_(bundle_id)) {
            return InstallStatus::AppRunning;
        }
    }

    const std::string tx_id = "tx_" + security::SecureRandom::get_random_hex(8);
    const std::string target_dir = "/Applications/" + bundle_id + ".app";
    const std::string backup_dir = "/backup/" + tx_id;
    const std::string staging_tx = "/staging/" + tx_id;
    const std::string stage_bundle_dir = staging_tx + "/bundle";

    if (!vfs_->exists("/staging")) vfs_->mkdir("/staging");
    if (!vfs_->exists("/backup")) vfs_->mkdir("/backup");
    if (!vfs_->mkdir(staging_tx)) return InstallStatus::StagingFailed;

    auto do_transition = [&](InstallerState state) -> bool {
        write_journal(tx_id, bundle_id, state, true, target_dir, backup_dir);
        if (fault_hook_) fault_hook_(state);
        if (inject_fault_state_.has_value() && inject_fault_state_.value() == state) {
            return false;
        }
        return true;
    };

    // 1. Transition: Initiated
    if (!do_transition(InstallerState::Initiated)) {
        return InstallStatus::CommitFailed;
    }

    // 2. Transition: Validating
    if (!do_transition(InstallerState::Validating)) {
        return InstallStatus::CommitFailed;
    }

    // 3. Transition: ManifestParsed
    if (!do_transition(InstallerState::ManifestParsed)) {
        return InstallStatus::CommitFailed;
    }

    // Validate canonical path for every file
    std::vector<std::pair<std::string, const FileEntry*>> normalized_files;
    for (const auto& f : pkg.manifest.files) {
        std::string norm_path;
        if (CanonicalPathValidator::validate_and_normalize_bundle_path(f.path, norm_path) != PathValidationStatus::Valid) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
            rollback_staging(staging_tx);
            return InstallStatus::InvalidManifest;
        }
        normalized_files.push_back({norm_path, &f});
    }

    // 4. Transition: SecurityVerified
    if (!do_transition(InstallerState::SecurityVerified)) {
        return InstallStatus::CommitFailed;
    }

    // 5. Transition: Staged
    if (!do_transition(InstallerState::Staged)) {
        return InstallStatus::CommitFailed;
    }

    if (!vfs_->mkdir(stage_bundle_dir)) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::StagingFailed;
    }

    if (injected_failure_ == InjectedFailureStage::StagingWrite) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::StagingFailed;
    }

    // 6. Transition: Extracting
    if (!do_transition(InstallerState::Extracting)) {
        return InstallStatus::CommitFailed;
    }

    for (const auto& [norm_path, f_ptr] : normalized_files) {
        const std::string stage_file = stage_bundle_dir + "/" + norm_path;
        const auto slash_pos = norm_path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            std::string parent_builder = stage_bundle_dir;
            std::istringstream pss(norm_path.substr(0, slash_pos));
            std::string seg;
            while (std::getline(pss, seg, '/')) {
                if (seg.empty()) continue;
                parent_builder += "/" + seg;
                if (!vfs_->exists(parent_builder)) vfs_->mkdir(parent_builder);
            }
        }
        if (!vfs_->create_file(stage_file) ||
            (f_ptr->size > 0 && !vfs_->write_file(stage_file, f_ptr->data.data(), f_ptr->data.size()))) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
            rollback_staging(staging_tx);
            return InstallStatus::StagingFailed;
        }
    }

    // 7. Transition: Extracted & Pre-commit integrity check
    if (!do_transition(InstallerState::Extracted)) {
        return InstallStatus::CommitFailed;
    }

    if (injected_failure_ == InjectedFailureStage::PreCommitIntegrity) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::IntegrityCheckFailed;
    }

    for (const auto& [norm_path, f_ptr] : normalized_files) {
        const std::string stage_file = stage_bundle_dir + "/" + norm_path;
        std::vector<std::uint8_t> read_back;
        if (!vfs_->read_file(stage_file, read_back)) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
            rollback_staging(staging_tx);
            return InstallStatus::IntegrityCheckFailed;
        }

        const std::string computed = crypto::Sha256::hash_hex(read_back);
        if (!f_ptr->sha256_hex.empty() && !security::constant_time_equals(computed, f_ptr->sha256_hex)) {
            write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
            rollback_staging(staging_tx);
            return InstallStatus::IntegrityCheckFailed;
        }
    }

    // 8. Transition: Registering (atomic commit phase)
    if (!do_transition(InstallerState::Registering)) {
        return InstallStatus::CommitFailed;
    }

    if (injected_failure_ == InjectedFailureStage::CommitMove) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    // Move current target to backup directory
    if (!vfs_->rename(target_dir, backup_dir)) {
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::CommitFailed;
    }

    // Move staging bundle to target
    bool move_ok = vfs_->rename(stage_bundle_dir, target_dir);
    if (!move_ok || injected_failure_ == InjectedFailureStage::RegistryCommit) {
        // Rollback from backup!
        remove_tree(*vfs_, target_dir);
        vfs_->rename(backup_dir, target_dir);
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::RollbackSuccess;
    }

    // Update registry record
    AppRegistration updated = *existing_opt;
    updated.name = pkg.manifest.name;
    updated.version = pkg.manifest.version;
    updated.exec_path = pkg.manifest.executable_name;
    updated.icon_path = pkg.manifest.icon_path;
    updated.startup_config = pkg.manifest.startup_config;
    updated.storage_quota_bytes = pkg.manifest.storage_quota_bytes;
    updated.permissions = pkg.manifest.permissions;
    updated.optional_permissions = pkg.manifest.optional_permissions;
    updated.update_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    if (!registry_->update_app(updated)) {
        remove_tree(*vfs_, target_dir);
        vfs_->rename(backup_dir, target_dir);
        write_journal(tx_id, bundle_id, InstallerState::Aborted, true, target_dir, backup_dir);
        rollback_staging(staging_tx);
        return InstallStatus::RollbackSuccess;
    }

    // 9. Transition: Committed
    if (!do_transition(InstallerState::Committed)) {
        return InstallStatus::CommitFailed;
    }

    // Cleanup backup and staging
    remove_tree(*vfs_, backup_dir);
    rollback_staging(staging_tx);
    return InstallStatus::Success;
}

InstallStatus PackageInstaller::uninstall(const std::string& bundle_id, bool purge_data) {
    if (!vfs_ || !registry_) return InstallStatus::VfsError;

    if (!registry_->is_installed(bundle_id)) {
        return InstallStatus::NotInstalled;
    }

    // If app is currently running, terminate it first
    if (is_running_cb_ && is_running_cb_(bundle_id)) {
        if (terminate_cb_) {
            terminate_cb_(bundle_id);
        }
    }

    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    remove_tree(*vfs_, app_dir);

    if (purge_data) {
        const std::string data_dir = "/data/" + bundle_id;
        remove_tree(*vfs_, data_dir);
    }

    registry_->unregister_app(bundle_id);
    return InstallStatus::Success;
}

} // namespace winios::packaging
