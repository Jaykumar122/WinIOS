#pragma once

#include "packaging/app_registry.h"
#include "loader/app_inspector.h"
#include "kernel/process_identity.h"
#include "settings/task_manager.h"
#include "platform/resource_budget.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "vfs/vfs.h"

#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace winios::loader {

enum class LaunchStatus : uint32_t {
    Success = 0,
    AppNotInstalled = 1,
    InstallationCorrupted = 2,
    ExecutableInvalid = 3,
    CompatibilityMismatch = 4,
    CapabilityDenied = 5,
    ResourceExhausted = 6,
    ProcessAllocationFailed = 7,
    TaskCreationFailed = 8
};

inline const char* launch_status_to_string(LaunchStatus status) noexcept {
    switch (status) {
    case LaunchStatus::Success: return "Success";
    case LaunchStatus::AppNotInstalled: return "AppNotInstalled";
    case LaunchStatus::InstallationCorrupted: return "InstallationCorrupted";
    case LaunchStatus::ExecutableInvalid: return "ExecutableInvalid";
    case LaunchStatus::CompatibilityMismatch: return "CompatibilityMismatch";
    case LaunchStatus::CapabilityDenied: return "CapabilityDenied";
    case LaunchStatus::ResourceExhausted: return "ResourceExhausted";
    case LaunchStatus::ProcessAllocationFailed: return "ProcessAllocationFailed";
    case LaunchStatus::TaskCreationFailed: return "TaskCreationFailed";
    default: return "Unknown";
    }
}

struct LaunchResult {
    LaunchStatus          status{LaunchStatus::AppNotInstalled};
    std::string           rejection_reason;
    kernel::ProcessHandle handle{0, 0};
    uint64_t              instance_id{0};
    uint64_t              entry_point{0};
    size_t                binary_size{0};
};

class AppLaunchPipeline {
public:
    static constexpr uint32_t kCurrentPlatformSdkVersion = 0x00020400; // SDK v2.4

    AppLaunchPipeline(std::shared_ptr<vfs::VirtualFileSystem> vfs,
                      std::shared_ptr<packaging::ApplicationRegistry> registry,
                      std::shared_ptr<kernel::ProcessIdentityManager> identity_mgr,
                      std::shared_ptr<settings::TaskManager> task_mgr,
                      std::shared_ptr<platform::ResourceBudgetManager> budget_mgr,
                      std::shared_ptr<security::CapabilityManager> cap_mgr,
                      std::shared_ptr<services::DiagnosticsService> diag_svc)
        : vfs_(std::move(vfs)),
          registry_(std::move(registry)),
          identity_mgr_(std::move(identity_mgr)),
          task_mgr_(std::move(task_mgr)),
          budget_mgr_(std::move(budget_mgr)),
          cap_mgr_(std::move(cap_mgr)),
          diag_svc_(std::move(diag_svc)) {}

    ~AppLaunchPipeline() = default;

    LaunchResult launch(const std::string& bundle_id, uint64_t start_time_us = 0) {
        LaunchResult res{};

        // 1. Resolve application identity
        if (!registry_) {
            res.status = LaunchStatus::AppNotInstalled;
            res.rejection_reason = "No application registry available";
            record_failure(bundle_id, res);
            return res;
        }

        auto app_opt = registry_->get_app(bundle_id);
        if (!app_opt.has_value()) {
            res.status = LaunchStatus::AppNotInstalled;
            res.rejection_reason = "Application " + bundle_id + " is not installed";
            record_failure(bundle_id, res);
            return res;
        }
        const auto& app = *app_opt;

        // 2. Validate installation integrity
        if (vfs_ && !registry_->verify_installed_app_integrity(bundle_id, *vfs_)) {
            res.status = LaunchStatus::InstallationCorrupted;
            res.rejection_reason = "Installation directory or executable corrupted or missing";
            record_failure(bundle_id, res);
            return res;
        }

        // 3. Read and validate executable (Mach-O ARM64 inspection)
        const std::string exec_path = app.install_dir + "/" + app.exec_path;
        std::vector<uint8_t> binary_data;
        if (vfs_ && (!vfs_->read_file(exec_path, binary_data) || binary_data.empty())) {
            res.status = LaunchStatus::ExecutableInvalid;
            res.rejection_reason = "Failed to read executable binary from VFS: " + exec_path;
            record_failure(bundle_id, res);
            return res;
        }

        AppInspectionReport report{};
        if (!binary_data.empty()) {
            report = AppInspector::inspect_binary(binary_data.data(), binary_data.size());
            if (!report.is_valid) {
                res.status = LaunchStatus::ExecutableInvalid;
                res.rejection_reason = "Executable validation failed: " + report.rejection_reason;
                record_failure(bundle_id, res);
                return res;
            }
        }

        // 4. Check ABI & SDK compatibility
        if (app.min_sdk_version > kCurrentPlatformSdkVersion) {
            res.status = LaunchStatus::CompatibilityMismatch;
            res.rejection_reason = "App requires SDK version " + std::to_string(app.min_sdk_version) +
                                  " which exceeds platform SDK " + std::to_string(kCurrentPlatformSdkVersion);
            record_failure(bundle_id, res);
            return res;
        }

        // 5. Capability authorization check
        if (cap_mgr_) {
            for (const auto& perm : app.permissions) {
                if (perm == "SystemAdmin" || perm == "KernelAccess") {
                    res.status = LaunchStatus::CapabilityDenied;
                    res.rejection_reason = "Permission denied for restricted capability: " + perm;
                    record_failure(bundle_id, res);
                    return res;
                }
            }
        }

        // 6. Allocate process identity
        if (!identity_mgr_) {
            res.status = LaunchStatus::ProcessAllocationFailed;
            res.rejection_reason = "Process identity manager unavailable";
            record_failure(bundle_id, res);
            return res;
        }

        kernel::ApplicationIdentity app_id{app.bundle_id, app.version, app.display_name, app.min_sdk_version};
        kernel::ProcessHandle handle = identity_mgr_->allocate_process(app_id, start_time_us);
        if (!handle.is_valid()) {
            res.status = LaunchStatus::ResourceExhausted;
            res.rejection_reason = "Process identity slots exhausted";
            record_failure(bundle_id, res);
            return res;
        }

        // 7. Register task in TaskManager
        if (task_mgr_) {
            settings::TaskInfo tinfo{};
            tinfo.pid = handle.pid;
            tinfo.generation = handle.generation;
            tinfo.name = app.name;
            tinfo.state = settings::TaskState::Running;
            tinfo.memory_bytes = binary_data.size();
            if (!task_mgr_->register_task(tinfo)) {
                // Rollback step 6
                identity_mgr_->terminate_process(handle);
                res.status = LaunchStatus::TaskCreationFailed;
                res.rejection_reason = "Failed to register task in TaskManager";
                record_failure(bundle_id, res);
                return res;
            }
        }

        // 8. Allocate resource budget
        if (budget_mgr_) {
            auto err = budget_mgr_->allocate_memory(handle.pid, binary_data.empty() ? 4096 : binary_data.size());
            if (!err.is_ok()) {
                if (task_mgr_) task_mgr_->unregister_task(handle.pid);
                identity_mgr_->terminate_process(handle);
                res.status = LaunchStatus::ResourceExhausted;
                res.rejection_reason = "Resource budget allocation failed: " + err.detail();
                record_failure(bundle_id, res);
                return res;
            }
        }

        // 9. Bind capabilities
        if (cap_mgr_) {
            cap_mgr_->bind_identity(handle.pid, app.bundle_id);
            for (const auto& perm : app.permissions) {
                if (perm == "SettingsRead") cap_mgr_->grant(handle.pid, security::Capability::SettingsRead);
                else if (perm == "SettingsWrite") cap_mgr_->grant(handle.pid, security::Capability::SettingsWrite);
                else if (perm == "TaskManage") cap_mgr_->grant(handle.pid, security::Capability::TaskManage);
                else if (perm == "AccessibilityAccess") cap_mgr_->grant(handle.pid, security::Capability::AccessibilityAccess);
                else if (perm == "DocumentRead") cap_mgr_->grant(handle.pid, security::Capability::DocumentRead);
                else if (perm == "DocumentWrite") cap_mgr_->grant(handle.pid, security::Capability::DocumentWrite);
                else if (perm == "DocumentCreate") cap_mgr_->grant(handle.pid, security::Capability::DocumentCreate);
                else if (perm == "DocumentDelete") cap_mgr_->grant(handle.pid, security::Capability::DocumentDelete);
                else if (perm == "DocumentShare") cap_mgr_->grant(handle.pid, security::Capability::DocumentShare);
            }
        }

        // 10. Record success diagnostics
        if (diag_svc_) {
            diag_svc_->log(
                bundle_id,
                handle.pid,
                services::LogLevel::Info,
                "Successfully launched " + bundle_id + " under PID " + std::to_string(handle.pid)
            );
            diag_svc_->record_event(
                1 /* Category: Task */,
                1 /* Level: Info */,
                handle.pid,
                0,
                "LAUNCH"
            );
        }

        auto inst_opt = identity_mgr_->get_instance(handle);
        res.status = LaunchStatus::Success;
        res.handle = handle;
        res.instance_id = inst_opt.has_value() ? inst_opt->instance_id : 0;
        res.entry_point = report.entry_point_offset;
        res.binary_size = binary_data.size();
        return res;
    }

    bool terminate(kernel::ProcessHandle handle) {
        if (!identity_mgr_ || !identity_mgr_->is_valid_handle(handle)) {
            return false;
        }

        uint32_t pid = handle.pid;

        // 1. Unbind capabilities
        if (cap_mgr_) {
            cap_mgr_->unbind_identity(pid);
        }

        // 2. Unregister task
        if (task_mgr_) {
            task_mgr_->unregister_task(pid);
        }

        // 3. Clear budget
        if (budget_mgr_) {
            budget_mgr_->release_all_task_resources(pid);
        }

        // 4. Terminate process identity (enters quarantine queue)
        bool ok = identity_mgr_->terminate_process(handle);

        if (diag_svc_) {
            diag_svc_->log(
                "",
                pid,
                services::LogLevel::Info,
                "Terminated process " + std::to_string(pid)
            );
            diag_svc_->record_event(
                1 /* Category: Task */,
                1 /* Level: Info */,
                pid,
                0,
                "TERM"
            );
        }
        return ok;
    }

private:
    void record_failure(const std::string& bundle_id, const LaunchResult& res) {
        if (diag_svc_) {
            diag_svc_->log(
                bundle_id,
                0,
                services::LogLevel::Error,
                "Launch failure: " + res.rejection_reason
            );
            diag_svc_->record_event(
                1 /* Category: Task */,
                3 /* Level: Error */,
                0,
                static_cast<int32_t>(res.status),
                "LAUNCH_FAIL"
            );
        }
    }

    std::shared_ptr<vfs::VirtualFileSystem>           vfs_;
    std::shared_ptr<packaging::ApplicationRegistry>   registry_;
    std::shared_ptr<kernel::ProcessIdentityManager>   identity_mgr_;
    std::shared_ptr<settings::TaskManager>            task_mgr_;
    std::shared_ptr<platform::ResourceBudgetManager>  budget_mgr_;
    std::shared_ptr<security::CapabilityManager>      cap_mgr_;
    std::shared_ptr<services::DiagnosticsService>     diag_svc_;
};

} // namespace winios::loader
