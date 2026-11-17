#include "packaging/package_installer.h"
#include "crypto/sha256.h"

#include <chrono>

namespace winios::packaging {

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
    std::vector<vfs::VfsDirEntry> entries;
    if (vfs_->readdir(staging_dir, entries)) {
        for (const auto& e : entries) {
            vfs_->unlink(staging_dir + "/" + e.name);
        }
    }
    vfs_->rmdir(staging_dir);
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
        // create subdirectories if needed
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
        // clean up existing directory
        std::vector<vfs::VfsDirEntry> old_entries;
        if (vfs_->readdir(target_dir, old_entries)) {
            for (const auto& e : old_entries) {
                vfs_->unlink(target_dir + "/" + e.name);
            }
        }
        vfs_->rmdir(target_dir);
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
            // rollback target dir
            std::vector<vfs::VfsDirEntry> tgt_entries;
            if (vfs_->readdir(target_dir, tgt_entries)) {
                for (const auto& e : tgt_entries) vfs_->unlink(target_dir + "/" + e.name);
            }
            vfs_->rmdir(target_dir);
            return InstallStatus::CommitFailed;
        }
    }

    // Clean up staging
    rollback_staging(staging_dir);

    // 6. Register in ApplicationRegistry
    AppRegistration reg{};
    reg.bundle_id = bundle_id;
    reg.name = pkg.manifest.name;
    reg.version = pkg.manifest.version;
    reg.exec_path = pkg.manifest.executable_name;
    reg.install_dir = target_dir;
    reg.permissions = pkg.manifest.permissions;
    reg.install_time_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    reg.is_active = true;

    if (!registry_->register_app(reg)) {
        return InstallStatus::CommitFailed;
    }

    return InstallStatus::Success;
}

InstallStatus PackageInstaller::uninstall(const std::string& bundle_id, bool purge_data) {
    if (!vfs_ || !registry_) return InstallStatus::VfsError;

    if (!registry_->is_installed(bundle_id)) {
        return InstallStatus::NotInstalled;
    }

    const std::string app_dir = "/Applications/" + bundle_id + ".app";
    if (vfs_->exists(app_dir)) {
        std::vector<vfs::VfsDirEntry> entries;
        if (vfs_->readdir(app_dir, entries)) {
            for (const auto& e : entries) {
                vfs_->unlink(app_dir + "/" + e.name);
            }
        }
        vfs_->rmdir(app_dir);
    }

    if (purge_data) {
        const std::string data_dir = "/data/" + bundle_id;
        if (vfs_->exists(data_dir)) {
            std::vector<vfs::VfsDirEntry> entries;
            if (vfs_->readdir(data_dir, entries)) {
                for (const auto& e : entries) {
                    vfs_->unlink(data_dir + "/" + e.name);
                }
            }
            vfs_->rmdir(data_dir);
        }
    }

    registry_->unregister_app(bundle_id);
    return InstallStatus::Success;
}

} // namespace winios::packaging
