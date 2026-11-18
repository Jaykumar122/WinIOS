#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <memory>
#include <optional>
#include <functional>

#include "kernel/app_lifecycle_manager.h"
#include "security/capability_manager.h"
#include "packaging/app_registry.h"
#include "runtime/guest_ipc.h"
#include "graphics/window_manager.h"
#include "graphics/compositor.h"

namespace winios::kernel {

enum class CrashCategory : std::uint32_t {
    None = 0,
    IllegalInstruction = 1,
    MemoryFault = 2,
    Abort = 3,
    Timeout = 4,
    QuotaExceeded = 5,
    SecurityViolation = 6,
    UnhandledException = 7
};

inline const char* crash_category_to_string(CrashCategory cat) noexcept {
    switch (cat) {
    case CrashCategory::None: return "None";
    case CrashCategory::IllegalInstruction: return "IllegalInstruction";
    case CrashCategory::MemoryFault: return "MemoryFault";
    case CrashCategory::Abort: return "Abort";
    case CrashCategory::Timeout: return "Timeout";
    case CrashCategory::QuotaExceeded: return "QuotaExceeded";
    case CrashCategory::SecurityViolation: return "SecurityViolation";
    case CrashCategory::UnhandledException: return "UnhandledException";
    default: return "UnknownCrash";
    }
}

struct CrashRecord {
    std::uint32_t pid{0};
    std::string   bundle_id;
    int           exit_code{0};
    CrashCategory category{CrashCategory::None};
    std::uint64_t guest_pc{0};
    std::uint64_t instructions_retired{0};
    std::uint64_t timestamp_us{0};
    std::string   last_lifecycle_state;
    bool          resources_cleaned_up{false};
};

class AppSupervisor {
public:
    static constexpr std::size_t kMaxCrashLogEntries = 64;

    AppSupervisor(AppLifecycleManager* lifecycle_mgr = nullptr,
                  security::CapabilityManager* cap_mgr = nullptr,
                  packaging::ApplicationRegistry* registry = nullptr,
                  graphics::WindowManager* wm = nullptr,
                  graphics::Compositor* comp = nullptr);
    ~AppSupervisor() = default;

    // Process tracking
    void register_process(std::uint32_t pid, const std::string& bundle_id);
    void unregister_process(std::uint32_t pid);

    // Lifecycle control
    bool request_termination(std::uint32_t pid);
    bool force_kill(std::uint32_t pid, int exit_code = -1);

    // Crash recording and cleanup
    void record_crash(std::uint32_t pid,
                      CrashCategory category,
                      int exit_code,
                      std::uint64_t pc = 0,
                      std::uint64_t retired_instructions = 0);

    // Resource cleanup after exit or crash
    void cleanup_process_resources(std::uint32_t pid);

    // Query crash records
    [[nodiscard]] std::vector<CrashRecord> get_recent_crashes(std::size_t max_count = 10) const;
    [[nodiscard]] std::size_t total_crashes() const;
    void clear_crashes();

    [[nodiscard]] bool is_running(const std::string& bundle_id) const;
    [[nodiscard]] std::optional<std::uint32_t> find_pid(const std::string& bundle_id) const;
    [[nodiscard]] std::size_t running_count() const;

private:
    AppLifecycleManager*            lifecycle_mgr_{nullptr};
    security::CapabilityManager*    cap_mgr_{nullptr};
    packaging::ApplicationRegistry* registry_{nullptr};
    graphics::WindowManager*        wm_{nullptr};
    graphics::Compositor*           comp_{nullptr};

    mutable std::mutex              lock_;
    std::unordered_map<std::uint32_t, std::string> pid_to_bundle_;
    std::deque<CrashRecord>         crash_log_;
    std::uint64_t                   total_crash_count_{0};
};

} // namespace winios::kernel
