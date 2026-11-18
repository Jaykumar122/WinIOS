#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <memory>

#include "security/capability_manager.h"
#include "kernel/app_supervisor.h"
#include "packaging/app_registry.h"
#include "graphics/window_manager.h"

namespace winios::services {

enum class LogLevel : std::uint32_t {
    Debug = 0,
    Info = 1,
    Warning = 2,
    Error = 3,
    Fatal = 4
};

inline const char* log_level_to_string(LogLevel lvl) noexcept {
    switch (lvl) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    case LogLevel::Fatal: return "FATAL";
    default: return "UNKNOWN";
    }
}

struct LogEntry {
    std::uint64_t sequence{0};
    std::uint64_t timestamp_us{0};
    std::string   bundle_id;
    std::uint32_t pid{0};
    LogLevel      level{LogLevel::Info};
    std::string   message;
};

struct PlatformDiagnosticsSummary {
    std::size_t active_processes{0};
    std::size_t installed_apps{0};
    std::size_t open_windows{0};
    std::size_t ipc_ports{0};
    std::size_t total_crashes{0};
    security::SecurityAuditStats security_stats;
};

class DiagnosticsService {
public:
    static constexpr std::size_t kMaxLogCapacity = 512;

    DiagnosticsService(kernel::AppSupervisor* supervisor = nullptr,
                       packaging::ApplicationRegistry* registry = nullptr,
                       graphics::WindowManager* wm = nullptr,
                       security::CapabilityManager* cap_mgr = nullptr);
    ~DiagnosticsService() = default;

    // Logging API
    void log(const std::string& bundle_id, std::uint32_t pid, LogLevel level, const std::string& message);

    // Query logs
    [[nodiscard]] std::vector<LogEntry> get_logs(std::size_t max_entries = 50, LogLevel min_level = LogLevel::Debug) const;
    [[nodiscard]] std::size_t total_log_count() const;
    void clear_logs();

    // Summary diagnostics
    [[nodiscard]] PlatformDiagnosticsSummary get_summary() const;

private:
    kernel::AppSupervisor*          supervisor_{nullptr};
    packaging::ApplicationRegistry* registry_{nullptr};
    graphics::WindowManager*        wm_{nullptr};
    security::CapabilityManager*    cap_mgr_{nullptr};

    mutable std::mutex              lock_;
    std::deque<LogEntry>            log_ring_;
    std::uint64_t                   sequence_counter_{0};
};

} // namespace winios::services
