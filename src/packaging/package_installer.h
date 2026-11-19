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

    // Recover interrupted installations by cleaning up any orphaned staging directories
    std::size_t recover_interrupted_installations();

    struct RecoveryReport {
        std::size_t abandoned_transactions_cleaned{0};
        std::size_t committed_transactions_finalized{0};
        std::size_t backups_restored{0};
        std::size_t corrupted_apps_flagged{0};
        std::size_t unindexed_apps_discovered{0};

        std::size_t total_recovered() const noexcept {
            return abandoned_transactions_cleaned + committed_transactions_finalized +
                   backups_restored + corrupted_apps_flagged + unindexed_apps_discovered;
        }
    };

    // Full reconciliation between VFS filesystem state, staging journals, and ApplicationRegistry
    RecoveryReport reconcile_and_recover();

    // Callbacks for running app lifecycle integration
    void set_running_check(RunningCheckCallback cb) { is_running_cb_ = std::move(cb); }
    void set_terminate_callback(TerminateCallback cb) { terminate_cb_ = std::move(cb); }

    // Helper: Compare semver strings ("major.minor.patch") -> returns -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
    static int compare_versions(const std::string& v1, const std::string& v2) noexcept;

    // Failure injection stages for deterministic testing of rollback and recovery
    enum class InjectedFailureStage : std::uint32_t {
        None = 0,
        StagingWrite = 1,
        PreCommitIntegrity = 2,
        CommitMove = 3,
        RegistryCommit = 4
    };

    void set_injected_failure(InjectedFailureStage stage) noexcept {
        injected_failure_ = stage;
    }

    enum class InstallerState : std::uint32_t {
        Idle = 0,
        Initiated = 1,
        Validating = 2,
        ManifestParsed = 3,
        SecurityVerified = 4,
        Staged = 5,
        Extracting = 6,
        Extracted = 7,
        Registering = 8,
        Committed = 9,
        Aborted = 10,
        // Compatibility aliases
        Pending = Initiated,
        Validated = SecurityVerified,
        Committing = Registering,
        RolledBack = Aborted
    };
    using TransactionState = InstallerState;

    static const char* installer_state_to_string(InstallerState s) noexcept {
        switch (s) {
        case InstallerState::Idle: return "Idle";
        case InstallerState::Initiated: return "Initiated";
        case InstallerState::Validating: return "Validating";
        case InstallerState::ManifestParsed: return "ManifestParsed";
        case InstallerState::SecurityVerified: return "SecurityVerified";
        case InstallerState::Staged: return "Staged";
        case InstallerState::Extracting: return "Extracting";
        case InstallerState::Extracted: return "Extracted";
        case InstallerState::Registering: return "Registering";
        case InstallerState::Committed: return "Committed";
        case InstallerState::Aborted: return "Aborted";
        default: return "Unknown";
        }
    }

    static const char* transaction_state_to_string(InstallerState s) noexcept {
        return installer_state_to_string(s);
    }

    using FaultInjectionHook = std::function<void(InstallerState)>;
    void set_fault_injection_hook(FaultInjectionHook hook) { fault_hook_ = std::move(hook); }
    void inject_fault_at_state(InstallerState state) noexcept { inject_fault_state_ = state; }
    void clear_fault_injection() noexcept { inject_fault_state_.reset(); fault_hook_ = nullptr; }

private:
    bool write_journal(const std::string& tx_id,
                       const std::string& bundle_id,
                       TransactionState state,
                       bool is_update,
                       const std::string& target_dir,
                       const std::string& backup_dir);

    void rollback_staging(const std::string& staging_dir);
    bool copy_directory(const std::string& src_dir, const std::string& dst_dir);

    std::shared_ptr<vfs::VirtualFileSystem> vfs_;
    std::shared_ptr<ApplicationRegistry>   registry_;
    RunningCheckCallback                    is_running_cb_;
    TerminateCallback                       terminate_cb_;
    InjectedFailureStage                    injected_failure_{InjectedFailureStage::None};
    FaultInjectionHook                      fault_hook_;
    std::optional<InstallerState>           inject_fault_state_;
};

} // namespace winios::packaging
