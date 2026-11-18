#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "cpu/cpu.h"
#include "kernel/bsd_process.h"
#include "kernel/scheduler.h"
#include "memory/user_address_space.h"

namespace winios::vfs {
class VirtualFileSystem;
}

namespace winios::graphics {
class WindowManager;
class SurfaceResourceManager;
class Compositor;
}

namespace winios::input {
class InputRouter;
}

namespace winios::runtime {
class GuestEventServices;
class GuestStorageApi;
class GuestAllocator;
class AppPreferences;
class SystemClipboard;
}

namespace winios::services {
class ConfigurationService;
class DiagnosticsService;
}

namespace winios::kernel {

class KernelThreadManager;
class MultiProcessScheduler;
class AppLifecycleManager;
class AppSupervisor;
}

namespace winios::security {
class CapabilityManager;
}

namespace winios::packaging {
class ApplicationRegistry;
}

namespace winios::documents {
class DocumentRegistry;
class DocumentStreamManager;
class FilePickerService;
class RecentDocumentManager;
}

namespace winios::settings {
class SystemStateManager;
class ControlCenterService;
class TaskManager;
class AccessibilityService;
}

namespace winios::kernel {

// Pinned Darwin/XNU Syscall Numbers (xnu-10002.1.13 bsd/sys/syscall.h)
inline constexpr std::uint64_t kSysExit       = 1;
inline constexpr std::uint64_t kSysFork       = 2;
inline constexpr std::uint64_t kSysRead       = 3;
inline constexpr std::uint64_t kSysWrite      = 4;
inline constexpr std::uint64_t kSysOpen       = 5;
inline constexpr std::uint64_t kSysClose      = 6;
inline constexpr std::uint64_t kSysWait4      = 7;
inline constexpr std::uint64_t kSysGetPid     = 20;
inline constexpr std::uint64_t kSysMachMsg    = 31;
inline constexpr std::uint64_t kSysGetPpid    = 39;
inline constexpr std::uint64_t kSysDup        = 41;
inline constexpr std::uint64_t kSysPipe       = 42;
inline constexpr std::uint64_t kSysLseek      = 199;
inline constexpr std::uint64_t kSysSchedYield = 329;
inline constexpr std::uint64_t kSysFstat64    = 338;
inline constexpr std::uint64_t kSysStat64     = 339;

// WinIOS Category C Display and Window System Calls
inline constexpr std::uint32_t kWiniosCategoryCAbiVersion = 0x00010100; // v1.1
inline constexpr std::uint64_t kSysWiniosWindowCreate      = 500;
inline constexpr std::uint64_t kSysWiniosWindowDestroy     = 501;
inline constexpr std::uint64_t kSysWiniosSurfaceUpdate     = 502;
inline constexpr std::uint64_t kSysWiniosSurfacePresent    = 503;
inline constexpr std::uint64_t kSysWiniosPollEvent         = 504;
inline constexpr std::uint64_t kSysWiniosSurfaceFill       = 505;
inline constexpr std::uint64_t kSysWiniosSetWindowTitle    = 506;
inline constexpr std::uint64_t kSysWiniosGetAbiVersion     = 507;
// Category C ABI v1.1 Extensions
inline constexpr std::uint64_t kSysWiniosWindowGetGeometry = 508;
inline constexpr std::uint64_t kSysWiniosWindowSetGeometry = 509;
inline constexpr std::uint64_t kSysWiniosWindowSetFocus    = 510;
inline constexpr std::uint64_t kSysWiniosWindowList        = 511;
inline constexpr std::uint64_t kSysWiniosCreateTimer       = 512;
// Category C ABI v1.2 Extensions
inline constexpr std::uint64_t kSysWiniosAllocHeap        = 513;
inline constexpr std::uint64_t kSysWiniosFreeHeap         = 514;
inline constexpr std::uint64_t kSysWiniosGetRuntimeInfo   = 515;
inline constexpr std::uint64_t kSysWiniosStorageRead      = 516;
inline constexpr std::uint64_t kSysWiniosStorageWrite     = 517;
inline constexpr std::uint64_t kSysWiniosIpcCreatePort    = 518;
inline constexpr std::uint64_t kSysWiniosIpcSend          = 519;
inline constexpr std::uint64_t kSysWiniosIpcRecv          = 520;
inline constexpr std::uint64_t kSysWiniosPrefGetSet       = 521;
// Category C ABI v1.3 Extensions (Multi-Thread, Mutex & Message Trap)
inline constexpr std::uint64_t kSysWiniosMutexLock        = 522;
inline constexpr std::uint64_t kSysWiniosMutexUnlock      = 523;
inline constexpr std::uint64_t kSysWiniosThreadCreate     = 524;
inline constexpr std::uint64_t kSysWiniosThreadExit       = 525;
inline constexpr std::uint64_t kSysWiniosThreadJoin       = 526;
inline constexpr std::uint64_t kSysWiniosMsgTrap          = 527;
// Category C ABI v1.5 Extensions (Campaign 221)
inline constexpr std::uint64_t kSysWiniosLog              = 528;
inline constexpr std::uint64_t kSysWiniosGetAppInfo       = 529;
inline constexpr std::uint64_t kSysWiniosSpawnProcess     = 530;
inline constexpr std::uint64_t kSysWiniosGetProcessStatus     = 531;
inline constexpr std::uint64_t kSysWiniosResourceOpen         = 532;
inline constexpr std::uint64_t kSysWiniosWindowSetVisibility = 533;
inline constexpr std::uint64_t kSysWiniosClipboardGet        = 534;
inline constexpr std::uint64_t kSysWiniosClipboardSet        = 535;
inline constexpr std::uint64_t kSysWiniosAppSupervisorNotify = 536;
// Category C ABI v1.8 Extensions (Campaign 224)
inline constexpr std::uint64_t kSysWiniosConfigGet         = 537;
inline constexpr std::uint64_t kSysWiniosConfigSet         = 538;
inline constexpr std::uint64_t kSysWiniosLogLeveled        = 539;
inline constexpr std::uint64_t kSysWiniosDiagSummary       = 540;
inline constexpr std::uint64_t kSysWiniosIpcCreatePortV2   = 541;
inline constexpr std::uint64_t kSysWiniosIpcSendV2         = 542;
inline constexpr std::uint64_t kSysWiniosIpcRecvV2         = 543;
inline constexpr std::uint64_t kSysWiniosIpcLookupPort     = 544;
inline constexpr std::uint64_t kSysWiniosIpcRegisterPort   = 545;
// Category C ABI v1.9 Extensions (Campaign 225 - Virtual Devices & System Services)
inline constexpr std::uint64_t kSysWiniosDeviceEnum        = 546;
inline constexpr std::uint64_t kSysWiniosDeviceGetInfo     = 547;
inline constexpr std::uint64_t kSysWiniosDeviceOpen        = 548;
inline constexpr std::uint64_t kSysWiniosDeviceClose       = 549;
inline constexpr std::uint64_t kSysWiniosDisplayGetConfig  = 550;
inline constexpr std::uint64_t kSysWiniosDisplaySetConfig  = 551;
inline constexpr std::uint64_t kSysWiniosAudioSubmit       = 552;
inline constexpr std::uint64_t kSysWiniosAudioGetStatus    = 553;
inline constexpr std::uint64_t kSysWiniosSensorSample      = 554;
inline constexpr std::uint64_t kSysWiniosSensorSetEnabled  = 555;
inline constexpr std::uint64_t kSysWiniosNetSend           = 556;
inline constexpr std::uint64_t kSysWiniosNetRecv           = 557;
inline constexpr std::uint64_t kSysWiniosTimeGet           = 558;
inline constexpr std::uint64_t kSysWiniosNotificationPost  = 559;
inline constexpr std::uint64_t kSysWiniosDeviceDiagSummary = 560;

// Category C ABI v2.0 Extensions (Campaign 226 - Multi-Window Compositor & Desktop Events)
inline constexpr std::uint64_t kSysWiniosWindowCreateV2       = 561;
inline constexpr std::uint64_t kSysWiniosWindowDestroyV2      = 562;
inline constexpr std::uint64_t kSysWiniosWindowSetState       = 563;
inline constexpr std::uint64_t kSysWiniosWindowGetState       = 564;
inline constexpr std::uint64_t kSysWiniosWindowInvalidateRect = 565;
inline constexpr std::uint64_t kSysWiniosEventPollV2          = 566;
inline constexpr std::uint64_t kSysWiniosEventWait             = 567;
inline constexpr std::uint64_t kSysWiniosTextInputSubmit      = 568;
inline constexpr std::uint64_t kSysWiniosClipboardGetV2       = 569;
inline constexpr std::uint64_t kSysWiniosClipboardSetV2       = 570;
// Category C ABI v2.1 Extensions (Campaign 227)
inline constexpr std::uint64_t kSysWiniosDocOpenPicker        = 571;
inline constexpr std::uint64_t kSysWiniosDocSavePicker        = 572;
inline constexpr std::uint64_t kSysWiniosDocGetMetadata       = 573;
inline constexpr std::uint64_t kSysWiniosDocStreamOpen        = 574;
inline constexpr std::uint64_t kSysWiniosDocStreamRead        = 575;
inline constexpr std::uint64_t kSysWiniosDocStreamWrite       = 576;
inline constexpr std::uint64_t kSysWiniosDocStreamSeek        = 577;
inline constexpr std::uint64_t kSysWiniosDocStreamClose       = 578;
inline constexpr std::uint64_t kSysWiniosDocGrantAccess       = 579;
inline constexpr std::uint64_t kSysWiniosDocGetRecent         = 580;

// Category C ABI v2.2 Extensions (Campaign 228)
inline constexpr std::uint64_t kSysWiniosSettingsGet          = 581;
inline constexpr std::uint64_t kSysWiniosSettingsSet          = 582;
inline constexpr std::uint64_t kSysWiniosThemeGet             = 583;
inline constexpr std::uint64_t kSysWiniosThemeSet             = 584;
inline constexpr std::uint64_t kSysWiniosControlCenterToggle  = 585;
inline constexpr std::uint64_t kSysWiniosTaskEnumerate        = 586;
inline constexpr std::uint64_t kSysWiniosTaskSwitch           = 587;
inline constexpr std::uint64_t kSysWiniosTaskTerminate        = 588;
inline constexpr std::uint64_t kSysWiniosAccessibilityGet     = 589;
inline constexpr std::uint64_t kSysWiniosAccessibilitySet     = 590;
inline constexpr std::uint64_t kSysWiniosDiagEventQuery        = 591;
inline constexpr std::uint64_t kSysWiniosMaxCategoryC         = 591;

[[nodiscard]] constexpr bool is_category_c_extension(std::uint64_t sys_num) noexcept {
    return sys_num >= kSysWiniosWindowCreate && sys_num <= kSysWiniosMaxCategoryC;
}

// Pinned Darwin/XNU Errno Numbers (xnu-10002.1.13 bsd/sys/errno.h)
inline constexpr std::int32_t kErrnoEPERM   = 1;
inline constexpr std::int32_t kErrnoENOENT  = 2;
inline constexpr std::int32_t kErrnoESRCH   = 3;
inline constexpr std::int32_t kErrnoEIO     = 5;
inline constexpr std::int32_t kErrnoEBADF   = 9;
inline constexpr std::int32_t kErrnoECHILD  = 10;
inline constexpr std::int32_t kErrnoENOMEM  = 12;
inline constexpr std::int32_t kErrnoEACCES  = 13;
inline constexpr std::int32_t kErrnoEFAULT  = 14;
inline constexpr std::int32_t kErrnoEEXIST  = 17;
inline constexpr std::int32_t kErrnoENODEV  = 19;
inline constexpr std::int32_t kErrnoEINVAL  = 22;
inline constexpr std::int32_t kErrnoESPIPE  = 29;
inline constexpr std::int32_t kErrnoEPIPE   = 32;
inline constexpr std::int32_t kErrnoENOSYS  = 78;

enum class SyscallAbi : std::uint8_t {
    AutoDetect   = 0,
    DarwinStrict = 1,
    WinIOSFast   = 2
};

class VirtualConsole {
public:
    VirtualConsole() = default;
    ~VirtualConsole() = default;

    void write_bytes(const char* data, std::size_t len) {
        if (data && len > 0) {
            output_.append(data, len);
        }
    }

    [[nodiscard]] const std::string& get_output() const noexcept { return output_; }
    void clear_output() noexcept { output_.clear(); }

private:
    std::string output_;
};

struct SyscallStats {
    std::uint64_t total_syscalls{0};
    std::uint64_t total_writes{0};
    std::uint64_t total_getpids{0};
    std::uint64_t total_yields{0};
    std::uint64_t total_exits{0};
    std::uint64_t total_ipc_traps{0};
    std::uint64_t total_invalid_syscalls{0};
    std::uint64_t total_invalid_pointers{0};
    std::uint64_t total_display_syscalls{0};
    std::uint64_t total_input_polls{0};
    std::uint64_t total_mutex_locks{0};
    std::uint64_t total_mutex_unlocks{0};
    std::uint64_t total_mutex_contentions{0};
    std::uint64_t total_mutex_errors{0};
    std::uint64_t total_guest_logs{0};
    std::uint64_t total_process_spawns{0};
};

class UserSyscallDispatcher {
public:
    explicit UserSyscallDispatcher(VirtualConsole& console) : console_(console) {}
    ~UserSyscallDispatcher() = default;

    void set_abi_mode(SyscallAbi mode) noexcept { abi_mode_ = mode; }
    [[nodiscard]] SyscallAbi abi_mode() const noexcept { return abi_mode_; }
    [[nodiscard]] SyscallAbi last_used_abi() const noexcept { return last_used_abi_; }

    void set_process_manager(BsdProcessManager* mgr) noexcept { proc_mgr_ = mgr; }
    [[nodiscard]] BsdProcessManager* process_manager() noexcept { return proc_mgr_; }

    void set_vfs(vfs::VirtualFileSystem* vfs) noexcept { vfs_ = vfs; }
    [[nodiscard]] vfs::VirtualFileSystem* vfs() noexcept { return vfs_; }

    void set_window_manager(graphics::WindowManager* wm) noexcept { window_mgr_ = wm; }
    [[nodiscard]] graphics::WindowManager* window_manager() noexcept { return window_mgr_; }

    void set_surface_resource_manager(graphics::SurfaceResourceManager* srm) noexcept { surface_mgr_ = srm; }
    [[nodiscard]] graphics::SurfaceResourceManager* surface_resource_manager() noexcept { return surface_mgr_; }

    void set_compositor(graphics::Compositor* comp) noexcept { compositor_ = comp; }
    [[nodiscard]] graphics::Compositor* compositor() noexcept { return compositor_; }

    void set_input_router(input::InputRouter* router) noexcept { input_router_ = router; }
    [[nodiscard]] input::InputRouter* input_router() noexcept { return input_router_; }

    void set_event_services(runtime::GuestEventServices* ev) noexcept { event_services_ = ev; }
    [[nodiscard]] runtime::GuestEventServices* event_services() noexcept { return event_services_; }

    void set_storage_api(runtime::GuestStorageApi* storage) noexcept { storage_api_ = storage; }
    [[nodiscard]] runtime::GuestStorageApi* storage_api() noexcept { return storage_api_; }

    void set_allocator(runtime::GuestAllocator* alloc) noexcept { allocator_ = alloc; }
    [[nodiscard]] runtime::GuestAllocator* allocator() noexcept { return allocator_; }

    void set_preferences(runtime::AppPreferences* prefs) noexcept { preferences_ = prefs; }
    [[nodiscard]] runtime::AppPreferences* preferences() noexcept { return preferences_; }

    void set_thread_manager(KernelThreadManager* tm) noexcept { thread_mgr_ = tm; }
    [[nodiscard]] KernelThreadManager* thread_manager() noexcept { return thread_mgr_; }

    void set_multi_process_scheduler(MultiProcessScheduler* sched) noexcept { multi_sched_ = sched; }
    [[nodiscard]] MultiProcessScheduler* multi_process_scheduler() noexcept { return multi_sched_; }

    void set_capability_manager(security::CapabilityManager* cm) noexcept { cap_mgr_ = cm; }
    [[nodiscard]] security::CapabilityManager* capability_manager() noexcept { return cap_mgr_; }

    void set_app_registry(packaging::ApplicationRegistry* reg) noexcept { app_reg_ = reg; }
    [[nodiscard]] packaging::ApplicationRegistry* app_registry() noexcept { return app_reg_; }

    void set_system_clipboard(runtime::SystemClipboard* sc) noexcept { clipboard_ = sc; }
    [[nodiscard]] runtime::SystemClipboard* system_clipboard() noexcept { return clipboard_; }

    void set_lifecycle_manager(AppLifecycleManager* lm) noexcept { lifecycle_mgr_ = lm; }
    [[nodiscard]] AppLifecycleManager* lifecycle_manager() noexcept { return lifecycle_mgr_; }

    void set_configuration_service(services::ConfigurationService* cs) noexcept { config_service_ = cs; }
    [[nodiscard]] services::ConfigurationService* configuration_service() noexcept { return config_service_; }

    void set_diagnostics_service(services::DiagnosticsService* ds) noexcept { diag_service_ = ds; }
    [[nodiscard]] services::DiagnosticsService* diagnostics_service() noexcept { return diag_service_; }

    void set_app_supervisor(AppSupervisor* as) noexcept { supervisor_ = as; }
    [[nodiscard]] AppSupervisor* app_supervisor() noexcept { return supervisor_; }

    void set_document_registry(documents::DocumentRegistry* dr) noexcept { doc_registry_ = dr; }
    [[nodiscard]] documents::DocumentRegistry* document_registry() noexcept { return doc_registry_; }

    void set_document_stream_manager(documents::DocumentStreamManager* dsm) noexcept { doc_stream_mgr_ = dsm; }
    [[nodiscard]] documents::DocumentStreamManager* document_stream_manager() noexcept { return doc_stream_mgr_; }

    void set_file_picker_service(documents::FilePickerService* fps) noexcept { file_picker_svc_ = fps; }
    [[nodiscard]] documents::FilePickerService* file_picker_service() noexcept { return file_picker_svc_; }

    void set_recent_document_manager(documents::RecentDocumentManager* rdm) noexcept { recent_doc_mgr_ = rdm; }
    [[nodiscard]] documents::RecentDocumentManager* recent_document_manager() noexcept { return recent_doc_mgr_; }

    void set_system_state_manager(settings::SystemStateManager* sm) noexcept { state_mgr_ = sm; }
    [[nodiscard]] settings::SystemStateManager* system_state_manager() noexcept { return state_mgr_; }

    void set_control_center_service(settings::ControlCenterService* ccs) noexcept { cc_svc_ = ccs; }
    [[nodiscard]] settings::ControlCenterService* control_center_service() noexcept { return cc_svc_; }

    void set_task_manager(settings::TaskManager* tm) noexcept { task_mgr_ = tm; }
    [[nodiscard]] settings::TaskManager* task_manager() noexcept { return task_mgr_; }

    void set_accessibility_service(settings::AccessibilityService* as) noexcept { a11y_svc_ = as; }
    [[nodiscard]] settings::AccessibilityService* accessibility_service() noexcept { return a11y_svc_; }

    // Dispatches a system call invoked by an EL0 thread
    bool handle_syscall(CPU& cpu,
                        BsdProcess& proc,
                        memory::UserAddressSpace& uas,
                        PriorityScheduler& sched);

    [[nodiscard]] const SyscallStats& stats() const noexcept { return stats_; }
    [[nodiscard]] VirtualConsole& console() noexcept { return console_; }

    struct MutexRecord {
        std::uint64_t owner_tid{0};
        pid_t owner_pid{0};
        std::uint32_t contention_count{0};
        bool locked{false};
    };

    [[nodiscard]] const std::unordered_map<std::uint64_t, MutexRecord>& mutex_table() const noexcept { return mutex_table_; }
    void clear_mutexes_for_process(pid_t pid);

    void serialize(std::vector<std::uint8_t>& dst) const;
    bool deserialize(const std::uint8_t*& src, std::size_t& remaining);

private:
    VirtualConsole& console_;
    SyscallStats stats_{};
    SyscallAbi abi_mode_{SyscallAbi::AutoDetect};
    SyscallAbi last_used_abi_{SyscallAbi::DarwinStrict};
    BsdProcessManager* proc_mgr_{nullptr};
    vfs::VirtualFileSystem* vfs_{nullptr};
    graphics::WindowManager* window_mgr_{nullptr};
    graphics::SurfaceResourceManager* surface_mgr_{nullptr};
    graphics::Compositor* compositor_{nullptr};
    input::InputRouter* input_router_{nullptr};
    runtime::GuestEventServices* event_services_{nullptr};
    runtime::GuestStorageApi* storage_api_{nullptr};
    runtime::GuestAllocator* allocator_{nullptr};
    runtime::AppPreferences* preferences_{nullptr};
    KernelThreadManager* thread_mgr_{nullptr};
    MultiProcessScheduler* multi_sched_{nullptr};
    security::CapabilityManager* cap_mgr_{nullptr};
    packaging::ApplicationRegistry* app_reg_{nullptr};
    runtime::SystemClipboard* clipboard_{nullptr};
    AppLifecycleManager* lifecycle_mgr_{nullptr};
    services::ConfigurationService* config_service_{nullptr};
    services::DiagnosticsService* diag_service_{nullptr};
    AppSupervisor* supervisor_{nullptr};
    documents::DocumentRegistry* doc_registry_{nullptr};
    documents::DocumentStreamManager* doc_stream_mgr_{nullptr};
    documents::FilePickerService* file_picker_svc_{nullptr};
    documents::RecentDocumentManager* recent_doc_mgr_{nullptr};
    settings::SystemStateManager* state_mgr_{nullptr};
    settings::ControlCenterService* cc_svc_{nullptr};
    settings::TaskManager* task_mgr_{nullptr};
    settings::AccessibilityService* a11y_svc_{nullptr};
    std::unordered_map<std::uint64_t, MutexRecord> mutex_table_;
};

} // namespace winios::kernel
