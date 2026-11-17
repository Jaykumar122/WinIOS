#pragma once

#include <memory>
#include <string>

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
    VfsError = 7
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
    default: return "UnknownInstallStatus";
    }
}

class PackageInstaller {
public:
    explicit PackageInstaller(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                              std::shared_ptr<ApplicationRegistry> registry);
    ~PackageInstaller() = default;

    // Transactional installation with automated rollback on error
    InstallStatus install(const AppPackage& pkg);

    // Uninstall installed package
    InstallStatus uninstall(const std::string& bundle_id, bool purge_data = false);

private:
    void rollback_staging(const std::string& staging_dir);

    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    std::shared_ptr<ApplicationRegistry>   registry_;
};

} // namespace winios::packaging
