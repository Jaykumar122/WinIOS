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
#include <functional>

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
    TaskCreationFailed = 8,
    MemoryMappingFailed = 9,
    ThreadAllocationFailed = 10,
    EnvironmentStagingFailed = 11,
    SandboxCreationFailed = 12
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
    case LaunchStatus::MemoryMappingFailed: return "MemoryMappingFailed";
    case LaunchStatus::ThreadAllocationFailed: return "ThreadAllocationFailed";
    case LaunchStatus::EnvironmentStagingFailed: return "EnvironmentStagingFailed";
    case LaunchStatus::SandboxCreationFailed: return "SandboxCreationFailed";
    default: return "Unknown";
    }
}

enum class LaunchTransactionPhase : uint32_t {
    Uninitialized = 0,
    ResolvingIdentity = 1,
    VerifyingIntegrity = 2,
    InspectingBinary = 3,
    CheckingCompatibility = 4,
    AuthorizingCapabilities = 5,
    AllocatingProcess = 6,
    RegisteringTask = 7,
    AllocatingBudget = 8,
    StagingEnvironment = 9,
    MappingGuestMemory = 10,
    AllocatingExecutionThread = 11,
    InitializingSandbox = 12,
    Committed = 13,
    RolledBack = 14
};

inline const char* launch_transaction_phase_to_string(LaunchTransactionPhase phase) noexcept {
    switch (phase) {
    case LaunchTransactionPhase::Uninitialized: return "Uninitialized";
    case LaunchTransactionPhase::ResolvingIdentity: return "ResolvingIdentity";
    case LaunchTransactionPhase::VerifyingIntegrity: return "VerifyingIntegrity";
    case LaunchTransactionPhase::InspectingBinary: return "InspectingBinary";
    case LaunchTransactionPhase::CheckingCompatibility: return "CheckingCompatibility";
    case LaunchTransactionPhase::AuthorizingCapabilities: return "AuthorizingCapabilities";
    case LaunchTransactionPhase::AllocatingProcess: return "AllocatingProcess";
    case LaunchTransactionPhase::RegisteringTask: return "RegisteringTask";
    case LaunchTransactionPhase::AllocatingBudget: return "AllocatingBudget";
    case LaunchTransactionPhase::StagingEnvironment: return "StagingEnvironment";
    case LaunchTransactionPhase::MappingGuestMemory: return "MappingGuestMemory";
    case LaunchTransactionPhase::AllocatingExecutionThread: return "AllocatingExecutionThread";
    case LaunchTransactionPhase::InitializingSandbox: return "InitializingSandbox";
    case LaunchTransactionPhase::Committed: return "Committed";
    case LaunchTransactionPhase::RolledBack: return "RolledBack";
    default: return "Unknown";
    }
}

enum class LaunchFailureInjectionPoint : uint32_t {
    None = 0,
    FailIntegrityCheck,
    FailBinaryInspection,
    FailCompatibilityCheck,
    FailCapabilityAuth,
    FailProcessAllocation,
    FailTaskRegistration,
    FailBudgetAllocation,
    FailEnvironmentStaging,
    FailMemoryMapping,
    FailThreadAllocation,
    FailSandboxCreation
};

struct LaunchResult {
    LaunchStatus            status{LaunchStatus::AppNotInstalled};
    std::string             rejection_reason;
    kernel::ProcessHandle   handle{0, 0};
    uint64_t                instance_id{0};
    uint64_t                entry_point{0};
    size_t                  binary_size{0};
    LaunchTransactionPhase  final_phase{LaunchTransactionPhase::Uninitialized};
    bool                    rolled_back{false};
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

    virtual ~AppLaunchPipeline() = default;

    void inject_failure(LaunchFailureInjectionPoint point, LaunchStatus status = LaunchStatus::ResourceExhausted) {
        injection_point_ = point;
        injected_status_ = status;
    }

    void clear_failure_injection() {
        injection_point_ = LaunchFailureInjectionPoint::None;
        injected_status_ = LaunchStatus::Success;
    }

    LaunchTransactionPhase last_transaction_phase() const noexcept { return last_phase_; }
    bool was_rolled_back() const noexcept { return was_rolled_back_; }

    LaunchResult launch(const std::string& bundle_id, uint64_t start_time_us = 0) {
        LaunchResult res{};
        was_rolled_back_ = false;

        // Helper for transactional rollback
        bool process_allocated = false;
        bool task_registered = false;
        bool budget_allocated = false;
        bool capabilities_bound = false;
        bool env_staged = false;
        bool memory_mapped = false;
        bool thread_allocated = false;
        bool sandbox_initialized = false;

        kernel::ProcessHandle allocated_handle{0, 0};
        uint64_t allocated_memory_bytes = 0;

        auto rollback = [&](LaunchTransactionPhase failed_phase, LaunchStatus fail_status, const std::string& reason) {
            last_phase_ = LaunchTransactionPhase::RolledBack;
            was_rolled_back_ = true;

            if (sandbox_initialized) {
                // Teardown sandbox
                sandbox_initialized = false;
            }
            if (thread_allocated) {
                // Deallocate thread
                thread_allocated = false;
            }
            if (memory_mapped) {
                // Unmap guest memory
                memory_mapped = false;
            }
            if (env_staged) {
                // Clear staged environment
                env_staged = false;
            }
            if (capabilities_bound && allocated_handle.pid != 0) {
                if (cap_mgr_) {
                    cap_mgr_->unbind_identity(allocated_handle.pid);
                    cap_mgr_->revoke_all(allocated_handle.pid);
                }
                capabilities_bound = false;
            }
            if (budget_allocated && allocated_handle.pid != 0) {
                if (budget_mgr_) budget_mgr_->release_all_task_resources(allocated_handle.pid);
                budget_allocated = false;
            }
            if (task_registered && allocated_handle.pid != 0) {
                if (task_mgr_) task_mgr_->unregister_task(allocated_handle.pid);
                task_registered = false;
            }
            if (process_allocated && allocated_handle.is_valid()) {
                if (identity_mgr_) identity_mgr_->terminate_process(allocated_handle);
                process_allocated = false;
            }

            res.status = fail_status;
            res.rejection_reason = reason;
            res.handle = {0, 0};
            res.instance_id = 0;
            res.final_phase = LaunchTransactionPhase::RolledBack;
            res.rolled_back = true;
            record_failure(bundle_id, res, failed_phase);
            return res;
        };

        // Phase 1: Resolving Identity
        last_phase_ = LaunchTransactionPhase::ResolvingIdentity;
        if (!registry_) {
            return rollback(last_phase_, LaunchStatus::AppNotInstalled, "No application registry available");
        }

        auto app_opt = registry_->get_app(bundle_id);
        if (!app_opt.has_value()) {
            return rollback(last_phase_, LaunchStatus::AppNotInstalled, "Application " + bundle_id + " is not installed");
        }
        const auto& app = *app_opt;

        // Phase 2: Verifying Integrity
        last_phase_ = LaunchTransactionPhase::VerifyingIntegrity;
        if (injection_point_ == LaunchFailureInjectionPoint::FailIntegrityCheck) {
            return rollback(last_phase_, injected_status_, "Injected integrity check failure");
        }
        if (vfs_ && !registry_->verify_installed_app_integrity(bundle_id, *vfs_)) {
            return rollback(last_phase_, LaunchStatus::InstallationCorrupted, "Installation directory or executable corrupted or missing");
        }

        // Phase 3: Inspecting Binary
        last_phase_ = LaunchTransactionPhase::InspectingBinary;
        if (injection_point_ == LaunchFailureInjectionPoint::FailBinaryInspection) {
            return rollback(last_phase_, injected_status_, "Injected binary inspection failure");
        }

        const std::string exec_path = app.install_dir + "/" + app.exec_path;
        std::vector<uint8_t> binary_data;
        if (vfs_ && (!vfs_->read_file(exec_path, binary_data) || binary_data.empty())) {
            return rollback(last_phase_, LaunchStatus::ExecutableInvalid, "Failed to read executable binary from VFS: " + exec_path);
        }

        AppInspectionReport report{};
        if (!binary_data.empty()) {
            report = AppInspector::inspect_binary(binary_data.data(), binary_data.size());
            if (!report.is_valid) {
                return rollback(last_phase_, LaunchStatus::ExecutableInvalid, "Executable validation failed: " + report.rejection_reason);
            }
        }

        // Phase 4: Checking Compatibility
        last_phase_ = LaunchTransactionPhase::CheckingCompatibility;
        if (injection_point_ == LaunchFailureInjectionPoint::FailCompatibilityCheck) {
            return rollback(last_phase_, injected_status_, "Injected compatibility failure");
        }
        if (app.min_sdk_version > kCurrentPlatformSdkVersion) {
            return rollback(last_phase_, LaunchStatus::CompatibilityMismatch,
                            "App requires SDK version " + std::to_string(app.min_sdk_version) +
                            " which exceeds platform SDK " + std::to_string(kCurrentPlatformSdkVersion));
        }

        // Phase 5: Authorizing Capabilities
        last_phase_ = LaunchTransactionPhase::AuthorizingCapabilities;
        if (injection_point_ == LaunchFailureInjectionPoint::FailCapabilityAuth) {
            return rollback(last_phase_, injected_status_, "Injected capability authorization failure");
        }
        if (cap_mgr_) {
            for (const auto& perm : app.permissions) {
                if (perm == "SystemAdmin" || perm == "KernelAccess") {
                    return rollback(last_phase_, LaunchStatus::CapabilityDenied, "Permission denied for restricted capability: " + perm);
                }
            }
        }

        // Phase 6: Allocating Process Identity
        last_phase_ = LaunchTransactionPhase::AllocatingProcess;
        if (injection_point_ == LaunchFailureInjectionPoint::FailProcessAllocation) {
            return rollback(last_phase_, injected_status_, "Injected process allocation failure");
        }
        if (!identity_mgr_) {
            return rollback(last_phase_, LaunchStatus::ProcessAllocationFailed, "Process identity manager unavailable");
        }

        kernel::ApplicationIdentity app_id{app.bundle_id, app.version, app.display_name, app.min_sdk_version};
        allocated_handle = identity_mgr_->allocate_process(app_id, start_time_us);
        if (!allocated_handle.is_valid()) {
            return rollback(last_phase_, LaunchStatus::ResourceExhausted, "Process identity slots exhausted");
        }
        process_allocated = true;

        // Phase 7: Registering Task
        last_phase_ = LaunchTransactionPhase::RegisteringTask;
        if (injection_point_ == LaunchFailureInjectionPoint::FailTaskRegistration) {
            return rollback(last_phase_, injected_status_, "Injected task registration failure");
        }
        if (task_mgr_) {
            settings::TaskInfo tinfo{};
            tinfo.pid = allocated_handle.pid;
            tinfo.generation = allocated_handle.generation;
            tinfo.name = app.name;
            tinfo.state = settings::TaskState::Running;
            tinfo.memory_bytes = binary_data.size();
            if (!task_mgr_->register_task(tinfo)) {
                return rollback(last_phase_, LaunchStatus::TaskCreationFailed, "Failed to register task in TaskManager");
            }
            task_registered = true;
        }

        // Phase 8: Allocating Budget
        last_phase_ = LaunchTransactionPhase::AllocatingBudget;
        if (injection_point_ == LaunchFailureInjectionPoint::FailBudgetAllocation) {
            return rollback(last_phase_, injected_status_, "Injected budget allocation failure");
        }
        if (budget_mgr_) {
            allocated_memory_bytes = binary_data.empty() ? 4096 : binary_data.size();
            auto err = budget_mgr_->allocate_memory(allocated_handle.pid, allocated_memory_bytes);
            if (!err.is_ok()) {
                return rollback(last_phase_, LaunchStatus::ResourceExhausted, "Resource budget allocation failed: " + err.detail());
            }
            budget_allocated = true;
        }

        // Phase 9: Staging Environment
        last_phase_ = LaunchTransactionPhase::StagingEnvironment;
        if (injection_point_ == LaunchFailureInjectionPoint::FailEnvironmentStaging) {
            return rollback(last_phase_, LaunchStatus::EnvironmentStagingFailed, "Environment staging failed");
        }
        env_staged = true;

        // Phase 10: Mapping Guest Memory
        last_phase_ = LaunchTransactionPhase::MappingGuestMemory;
        if (injection_point_ == LaunchFailureInjectionPoint::FailMemoryMapping) {
            return rollback(last_phase_, LaunchStatus::MemoryMappingFailed, "Guest virtual memory mapping failed");
        }
        memory_mapped = true;

        // Phase 11: Allocating Execution Thread
        last_phase_ = LaunchTransactionPhase::AllocatingExecutionThread;
        if (injection_point_ == LaunchFailureInjectionPoint::FailThreadAllocation) {
            return rollback(last_phase_, LaunchStatus::ThreadAllocationFailed, "Host EL0 guest worker thread allocation failed");
        }
        thread_allocated = true;

        // Phase 12: Initializing Sandbox
        last_phase_ = LaunchTransactionPhase::InitializingSandbox;
        if (injection_point_ == LaunchFailureInjectionPoint::FailSandboxCreation) {
            return rollback(last_phase_, LaunchStatus::SandboxCreationFailed, "Guest process sandbox container creation failed");
        }
        sandbox_initialized = true;

        // Bind Capabilities
        if (cap_mgr_) {
            cap_mgr_->bind_identity(allocated_handle.pid, app.bundle_id);
            capabilities_bound = true;
            for (const auto& perm : app.permissions) {
                if (perm == "SettingsRead") cap_mgr_->grant(allocated_handle.pid, security::Capability::SettingsRead);
                else if (perm == "SettingsWrite") cap_mgr_->grant(allocated_handle.pid, security::Capability::SettingsWrite);
                else if (perm == "TaskManage") cap_mgr_->grant(allocated_handle.pid, security::Capability::TaskManage);
                else if (perm == "AccessibilityAccess") cap_mgr_->grant(allocated_handle.pid, security::Capability::AccessibilityAccess);
                else if (perm == "DocumentRead") cap_mgr_->grant(allocated_handle.pid, security::Capability::DocumentRead);
                else if (perm == "DocumentWrite") cap_mgr_->grant(allocated_handle.pid, security::Capability::DocumentWrite);
                else if (perm == "DocumentCreate") cap_mgr_->grant(allocated_handle.pid, security::Capability::DocumentCreate);
                else if (perm == "DocumentDelete") cap_mgr_->grant(allocated_handle.pid, security::Capability::DocumentDelete);
                else if (perm == "DocumentShare") cap_mgr_->grant(allocated_handle.pid, security::Capability::DocumentShare);
            }
        }

        // Phase 13: Atomic Commit
        last_phase_ = LaunchTransactionPhase::Committed;
        was_rolled_back_ = false;

        if (diag_svc_) {
            diag_svc_->log(
                bundle_id,
                allocated_handle.pid,
                services::LogLevel::Info,
                "Successfully launched " + bundle_id + " under PID " + std::to_string(allocated_handle.pid)
            );
            diag_svc_->record_event(
                1 /* Category: Task */,
                1 /* Level: Info */,
                allocated_handle.pid,
                0,
                "LAUNCH"
            );
        }

        auto inst_opt = identity_mgr_->get_instance(allocated_handle);
        res.status = LaunchStatus::Success;
        res.handle = allocated_handle;
        res.instance_id = inst_opt.has_value() ? inst_opt->instance_id : 0;
        res.entry_point = report.entry_point_offset;
        res.binary_size = binary_data.size();
        res.final_phase = LaunchTransactionPhase::Committed;
        res.rolled_back = false;
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

        // 4. Terminate process identity
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
    void record_failure(const std::string& bundle_id, const LaunchResult& res, LaunchTransactionPhase phase) {
        if (diag_svc_) {
            diag_svc_->log(
                bundle_id,
                0,
                services::LogLevel::Error,
                "Launch failure in phase " + std::string(launch_transaction_phase_to_string(phase)) + ": " + res.rejection_reason
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

    LaunchFailureInjectionPoint                       injection_point_{LaunchFailureInjectionPoint::None};
    LaunchStatus                                      injected_status_{LaunchStatus::Success};
    LaunchTransactionPhase                            last_phase_{LaunchTransactionPhase::Uninitialized};
    bool                                              was_rolled_back_{false};
};

using AppLauncher = AppLaunchPipeline;

} // namespace winios::loader
