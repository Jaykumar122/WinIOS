#include "services/diagnostics_service.h"
#include "devices/device_registry.h"
#include "devices/audio_service.h"
#include "devices/sensor_service.h"
#include "devices/network_service.h"
#include "devices/notification_service.h"
#include <chrono>

namespace winios::services {

DiagnosticsService::DiagnosticsService(kernel::AppSupervisor* supervisor,
                                       packaging::ApplicationRegistry* registry,
                                       graphics::WindowManager* wm,
                                       security::CapabilityManager* cap_mgr)
    : supervisor_(supervisor)
    , registry_(registry)
    , wm_(wm)
    , cap_mgr_(cap_mgr) {}

void DiagnosticsService::log(const std::string& bundle_id, std::uint32_t pid, LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> guard(lock_);
    const auto now_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    LogEntry entry{};
    entry.sequence = ++sequence_counter_;
    entry.timestamp_us = now_us;
    entry.bundle_id = bundle_id;
    entry.pid = pid;
    entry.level = level;
    entry.message = message;

    log_ring_.push_back(std::move(entry));
    while (log_ring_.size() > kMaxLogCapacity) {
        log_ring_.pop_front();
    }

    winios_diag_event_record_t ev{};
    ev.sequence = ++event_sequence_counter_;
    ev.timestamp_us = now_us;
    ev.category = 1; // System
    ev.level = static_cast<uint32_t>(level);
    ev.pid = pid;
    ev.error_code = 0;
    strncpy_s(ev.tag, sizeof(ev.tag), bundle_id.c_str(), _TRUNCATE);
    event_ring_.push_back(ev);
    while (event_ring_.size() > kMaxEventCapacity) {
        event_ring_.pop_front();
    }
}

std::vector<LogEntry> DiagnosticsService::get_logs(std::size_t max_entries, LogLevel min_level) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<LogEntry> res;
    for (auto it = log_ring_.rbegin(); it != log_ring_.rend() && res.size() < max_entries; ++it) {
        if (static_cast<std::uint32_t>(it->level) >= static_cast<std::uint32_t>(min_level)) {
            res.push_back(*it);
        }
    }
    return res;
}

std::size_t DiagnosticsService::total_log_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return sequence_counter_;
}

void DiagnosticsService::clear_logs() {
    std::lock_guard<std::mutex> guard(lock_);
    log_ring_.clear();
}

PlatformDiagnosticsSummary DiagnosticsService::get_summary() const {
    PlatformDiagnosticsSummary s{};
    if (supervisor_) {
        s.active_processes = supervisor_->running_count();
        s.total_crashes = supervisor_->total_crashes();
    }
    if (registry_) {
        s.installed_apps = registry_->app_count(false);
    }
    if (wm_) {
        s.open_windows = wm_->window_count();
    }
    s.ipc_ports = runtime::GuestIpcRouter::Get().GetPortCount();
    if (cap_mgr_) {
        s.security_stats = cap_mgr_->get_audit_stats();
    }
    return s;
}

DeviceDiagnosticsSummary DiagnosticsService::get_device_summary() const {
    DeviceDiagnosticsSummary ds{};
    auto& reg = devices::DeviceRegistry::get();
    const auto all = reg.enumerate_all();
    ds.total_devices = all.size();
    for (const auto& d : all) {
        if (d.state == devices::DeviceState::Running || d.state == devices::DeviceState::Ready) {
            ++ds.active_devices;
        }
    }

    if (auto dev = reg.get_device(devices::VirtualAudioService::kDefaultAudioDeviceId)) {
        if (auto audio = std::dynamic_pointer_cast<devices::VirtualAudioService>(dev)) {
            auto st = audio->get_status();
            ds.audio_bytes_submitted = st.total_bytes_submitted;
            ds.audio_underruns = st.total_underruns;
        }
    }

    if (auto dev = reg.get_device(devices::VirtualSensorService::kDefaultSensorDeviceId)) {
        if (auto sensor = std::dynamic_pointer_cast<devices::VirtualSensorService>(dev)) {
            ds.sensor_samples_generated = sensor->total_samples_generated();
        }
    }

    if (auto dev = reg.get_device(devices::VirtualNetworkService::kDefaultNetworkDeviceId)) {
        if (auto net = std::dynamic_pointer_cast<devices::VirtualNetworkService>(dev)) {
            auto st = net->get_stats();
            ds.network_packets_sent = st.packets_sent;
            ds.network_packets_received = st.packets_received;
            ds.network_packets_dropped = st.packets_dropped;
        }
    }

    if (auto dev = reg.get_device(devices::VirtualNotificationService::kDefaultNotificationDeviceId)) {
        if (auto notif = std::dynamic_pointer_cast<devices::VirtualNotificationService>(dev)) {
            ds.notifications_posted = notif->total_posted();
            ds.notifications_active = notif->active_count();
        }
    }

    return ds;
}

void DiagnosticsService::record_event(uint32_t category, uint32_t level, uint32_t pid, int32_t error_code, const char* tag) {
    std::lock_guard<std::mutex> guard(lock_);
    winios_diag_event_record_t ev{};
    ev.sequence = ++event_sequence_counter_;
    ev.timestamp_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    ev.category = category;
    ev.level = level;
    ev.pid = pid;
    ev.error_code = error_code;
    if (tag) {
        strncpy_s(ev.tag, sizeof(ev.tag), tag, _TRUNCATE);
    }
    event_ring_.push_back(ev);
    while (event_ring_.size() > kMaxEventCapacity) {
        event_ring_.pop_front();
    }
}

void DiagnosticsService::record_event(const winios_diag_event_record_t& ev) {
    std::lock_guard<std::mutex> guard(lock_);
    winios_diag_event_record_t copy = ev;
    if (copy.sequence == 0) {
        copy.sequence = ++event_sequence_counter_;
    }
    if (copy.timestamp_us == 0) {
        copy.timestamp_us = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    event_ring_.push_back(copy);
    while (event_ring_.size() > kMaxEventCapacity) {
        event_ring_.pop_front();
    }
}

std::vector<winios_diag_event_record_t> DiagnosticsService::query_events(
    uint64_t after_sequence,
    uint32_t category_filter,
    uint32_t min_level,
    uint32_t max_entries,
    uint32_t caller_pid,
    bool privileged) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<winios_diag_event_record_t> results;
    const uint32_t bounded_max = std::min<uint32_t>(max_entries, 128);
    results.reserve(bounded_max);

    for (const auto& ev : event_ring_) {
        if (ev.sequence <= after_sequence) continue;
        if (category_filter != 0 && ev.category != category_filter) continue;
        if (ev.level < min_level) continue;
        if (!privileged && ev.pid != caller_pid && ev.pid != 0) continue;

        results.push_back(ev);
        if (results.size() >= bounded_max) {
            break;
        }
    }
    return results;
}

std::size_t DiagnosticsService::total_event_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    return event_sequence_counter_;
}

void DiagnosticsService::clear_events() {
    std::lock_guard<std::mutex> guard(lock_);
    event_ring_.clear();
}

} // namespace winios::services
