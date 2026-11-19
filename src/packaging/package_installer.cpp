#include "packaging/package_installer.h"
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

InstallStatus PackageInstaller::install(const AppPackage& pkg) {
    if (!vfs_ || !registry_) return InstallStatus::VfsError;

    // 1. Validate manifest
    if (!pkg.manifest.is_valid()) {
        return InstallStatus::InvalidManifest;
    }

    const std::string& bundle_id = pkg.manifest.bundle_id;

    // 2. Check if already installed
    if (registry_->is_installed(bundle_id)) {
        return InstallStatus::AlreadyInstalled;
    }

    // 3. Verify file integrity (SHA-256)
    for (const auto& f : pkg.manifest.files) {
        const std::string computed_hash = crypto::Sha256::hash_hex(f.data);
        if (!f.sha256_hex.empty() && computed_hash != f.sha256_hex) {
            return InstallStatus::IntegrityCheckFailed;
        }
    }

    // 4. Staging
    const std::string staging_dir = "/staging/" + bundle_id;
    if (vfs_->exists(staging_dir)) {
        rollback_staging(staging_dir);
    }
    if (!vfs_->mkdir(staging_dir)) {
        return InstallStatus::StagingFailed;
    }

    for (const auto& f : pkg.manifest.files) {
        const std::string stage_file_path = staging_dir + "/" + f.path;
        const auto slash_pos = f.path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            const std::string sub = staging_dir + "/" + f.path.substr(0, slash_pos);
            if (!vfs_->exists(sub)) vfs_->mkdir(sub);
        }

        if (!vfs_->create_file(stage_file_path) ||
            (f.size > 0 && !vfs_->write_file(stage_file_path, f.data.data(), f.data.size()))) {
            rollback_staging(staging_dir);
            return InstallStatus::StagingFailed;
        }
    }

    // 5. Commit to /Applications/<bundle_id>.app/
    const std::string target_dir = "/Applications/" + bundle_id + ".app";
    if (vfs_->exists(target_dir)) {
        remove_tree(*vfs_, target_dir);
    }
    if (!vfs_->mkdir(target_dir)) {
        rollback_staging(staging_dir);
        return InstallStatus::CommitFailed;
    }

    for (const auto& f : pkg.manifest.files) {
        const std::string target_file_path = target_dir + "/" + f.path;
        const auto slash_pos = f.path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            const std::string sub = target_dir + "/" + f.path.substr(0, slash_pos);
            if (!vfs_->exists(sub)) vfs_->mkdir(sub);
        }

        if (!vfs_->create_file(target_file_path) ||
            (f.size > 0 && !vfs_->write_file(target_file_path, f.data.data(), f.data.size()))) {
            rollback_staging(staging_dir);
            remove_tree(*vfs_, target_dir);
            return InstallStatus::CommitFailed;
        }
    }

    // Clean up staging
    rollback_staging(staging_dir);

    // 6. Register in ApplicationRegistry
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
        return InstallStatus::CommitFailed;
    }

    for (const auto& dt : pkg.manifest.supported_doc_types) {
        registry_->set_default_app_for_type(dt, bundle_id);
    }

    return InstallStatus::Success;
}

std::size_t PackageInstaller::recover_interrupted_installations() {
    if (!vfs_ || !vfs_->exists("/staging")) return 0;
    std::vector<vfs::VfsDirEntry> entries;
    if (!vfs_->readdir("/staging", entries)) return 0;
    std::size_t recovered = 0;
    for (const auto& e : entries) {
        if (e.name == "." || e.name == "..") continue;
        const std::string path = "/staging/" + e.name;
        rollback_staging(path);
        ++recovered;
    }
    return recovered;
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

    // Verify integrity
    for (const auto& f : pkg.manifest.files) {
        const std::string computed = crypto::Sha256::hash_hex(f.data);
        if (!f.sha256_hex.empty() && computed != f.sha256_hex) {
            return InstallStatus::IntegrityCheckFailed;
        }
    }

    const std::string target_dir = "/Applications/" + bundle_id + ".app";
    const std::string staging_dir = "/staging/" + bundle_id + ".update";
    const std::string backup_dir = "/staging/" + bundle_id + ".backup";

    // Prepare staging
    if (vfs_->exists(staging_dir)) rollback_staging(staging_dir);
    if (vfs_->exists(backup_dir)) rollback_staging(backup_dir);

    if (!vfs_->mkdir(staging_dir)) return InstallStatus::StagingFailed;

    // Stage new files
    for (const auto& f : pkg.manifest.files) {
        const std::string stage_file = staging_dir + "/" + f.path;
        const auto slash_pos = f.path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            const std::string sub = staging_dir + "/" + f.path.substr(0, slash_pos);
            if (!vfs_->exists(sub)) vfs_->mkdir(sub);
        }
        if (!vfs_->create_file(stage_file) ||
            (f.size > 0 && !vfs_->write_file(stage_file, f.data.data(), f.data.size()))) {
            rollback_staging(staging_dir);
            return InstallStatus::StagingFailed;
        }
    }

    // Backup current installation
    if (!copy_directory(target_dir, backup_dir)) {
        rollback_staging(staging_dir);
        return InstallStatus::CommitFailed;
    }

    // Remove existing target and commit new files
    remove_tree(*vfs_, target_dir);
    if (!vfs_->mkdir(target_dir)) {
        // Rollback from backup!
        copy_directory(backup_dir, target_dir);
        rollback_staging(backup_dir);
        rollback_staging(staging_dir);
        return InstallStatus::RollbackSuccess;
    }

    bool commit_ok = true;
    for (const auto& f : pkg.manifest.files) {
        const std::string target_file = target_dir + "/" + f.path;
        const auto slash_pos = f.path.find_last_of('/');
        if (slash_pos != std::string::npos) {
            const std::string sub = target_dir + "/" + f.path.substr(0, slash_pos);
            if (!vfs_->exists(sub)) vfs_->mkdir(sub);
        }
        if (!vfs_->create_file(target_file) ||
            (f.size > 0 && !vfs_->write_file(target_file, f.data.data(), f.data.size()))) {
            commit_ok = false;
            break;
        }
    }

    if (!commit_ok) {
        // Rollback from backup
        remove_tree(*vfs_, target_dir);
        copy_directory(backup_dir, target_dir);
        rollback_staging(backup_dir);
        rollback_staging(staging_dir);
        return InstallStatus::RollbackSuccess;
    }

    // Success: cleanup backup and staging
    rollback_staging(backup_dir);
    rollback_staging(staging_dir);

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

    registry_->update_app(updated);
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
