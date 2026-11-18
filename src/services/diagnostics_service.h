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
#include "winios_sdk/winios_sdk.h"

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

#pragma pack(push, 8)
struct DeviceDiagnosticsSummary {
    std::size_t   total_devices{0};
    std::size_t   active_devices{0};
    std::uint64_t audio_bytes_submitted{0};
    std::uint64_t audio_underruns{0};
    std::uint64_t sensor_samples_generated{0};
    std::uint64_t network_packets_sent{0};
    std::uint64_t network_packets_received{0};
    std::uint64_t network_packets_dropped{0};
    std::uint64_t notifications_posted{0};
    std::size_t   notifications_active{0};
};
#pragma pack(pop)

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
    [[nodiscard]] DeviceDiagnosticsSummary get_device_summary() const;

    // ABI v2.3 Event Logging & Query API (Stages 229.10, 229.11)
    static constexpr std::size_t kMaxEventCapacity = 512;
    void record_event(uint32_t category, uint32_t level, uint32_t pid, int32_t error_code, const char* tag);
    void record_event(const winios_diag_event_record_t& ev);
    [[nodiscard]] std::vector<winios_diag_event_record_t> query_events(
        uint64_t after_sequence,
        uint32_t category_filter,
        uint32_t min_level,
        uint32_t max_entries,
        uint32_t caller_pid,
        bool privileged) const;
    [[nodiscard]] std::size_t total_event_count() const;
    void clear_events();

private:
    kernel::AppSupervisor*          supervisor_{nullptr};
    packaging::ApplicationRegistry* registry_{nullptr};
    graphics::WindowManager*        wm_{nullptr};
    security::CapabilityManager*    cap_mgr_{nullptr};

    mutable std::mutex                     lock_;
    std::deque<LogEntry>                   log_ring_;
    std::uint64_t                          sequence_counter_{0};
    std::deque<winios_diag_event_record_t> event_ring_;
    std::uint64_t                          event_sequence_counter_{0};
};

} // namespace winios::services
