#pragma once

#include <memory>
#include <string>
#include <functional>

#include "packaging/app_package.h"
#include "packaging/app_registry.h"
#include "vfs/vfs.h"

namespace winios::packaging {

enum class InstallStatus : std::uint32_t {
    Success = 0,
    InvalidManifest = 1,
    IntegrityCheckFailed = 2,
    AlreadyInstalled = 3,
    NotInstalled = 4,
    StagingFailed = 5,
    CommitFailed = 6,
    VfsError = 7,
    AppRunning = 8,
    VersionDowngradeRejected = 9,
    RollbackSuccess = 10,
    RollbackFailed = 11,
    PackageCorrupted = 12
};

inline const char* install_status_to_string(InstallStatus status) noexcept {
    switch (status) {
    case InstallStatus::Success: return "Success";
    case InstallStatus::InvalidManifest: return "InvalidManifest";
    case InstallStatus::IntegrityCheckFailed: return "IntegrityCheckFailed";
    case InstallStatus::AlreadyInstalled: return "AlreadyInstalled";
    case InstallStatus::NotInstalled: return "NotInstalled";
    case InstallStatus::StagingFailed: return "StagingFailed";
    case InstallStatus::CommitFailed: return "CommitFailed";
    case InstallStatus::VfsError: return "VfsError";
    case InstallStatus::AppRunning: return "AppRunning";
    case InstallStatus::VersionDowngradeRejected: return "VersionDowngradeRejected";
    case InstallStatus::RollbackSuccess: return "RollbackSuccess";
    case InstallStatus::RollbackFailed: return "RollbackFailed";
    case InstallStatus::PackageCorrupted: return "PackageCorrupted";
    default: return "UnknownInstallStatus";
    }
}

class PackageInstaller {
public:
    using RunningCheckCallback = std::function<bool(const std::string& bundle_id)>;
    using TerminateCallback = std::function<bool(const std::string& bundle_id)>;

    explicit PackageInstaller(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                              std::shared_ptr<ApplicationRegistry> registry);
    ~PackageInstaller() = default;

    // Transactional installation with automated rollback on error
    InstallStatus install(const AppPackage& pkg);

    // Transactional update with backup and automated rollback on failure
    InstallStatus update(const AppPackage& pkg, bool allow_downgrade = false);

    // Uninstall installed package (safe removal with data persistence preservation unless purged)
    InstallStatus uninstall(const std::string& bundle_id, bool purge_data = false);

    // Callbacks for running app lifecycle integration
    void set_running_check(RunningCheckCallback cb) { is_running_cb_ = std::move(cb); }
    void set_terminate_callback(TerminateCallback cb) { terminate_cb_ = std::move(cb); }

    // Helper: Compare semver strings ("major.minor.patch") -> returns -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
    static int compare_versions(const std::string& v1, const std::string& v2) noexcept;

private:
    void rollback_staging(const std::string& staging_dir);
    bool copy_directory(const std::string& src_dir, const std::string& dst_dir);

    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    std::shared_ptr<ApplicationRegistry>   registry_;
    RunningCheckCallback                    is_running_cb_;
    TerminateCallback                       terminate_cb_;
};

} // namespace winios::packaging
