#include "security/capability_manager.h"

namespace winios::security {

Capability CapabilityManager::parse_capability_name(const std::string& name) noexcept {
    if (name == "display.window" || name == "display") return Capability::DisplayWindow;
    if (name == "storage.private" || name == "storage") return Capability::StoragePrivate;
    if (name == "storage.shared") return Capability::StorageShared;
    if (name == "clipboard.read") return Capability::ClipboardRead;
    if (name == "clipboard.write") return Capability::ClipboardWrite;
    if (name == "clipboard") return static_cast<Capability>(Capability::ClipboardRead | Capability::ClipboardWrite);
    if (name == "process.spawn" || name == "spawn") return Capability::ProcessSpawn;
    if (name == "filesystem" || name == "fs") return Capability::FilesystemAccess;
    if (name == "ipc") return Capability::IpcAccess;
    if (name == "timer") return Capability::TimerAccess;
    if (name == "logging" || name == "log") return Capability::LoggingAccess;
    if (name == "resource" || name == "res") return Capability::ResourceAccess;
    if (name == "config") return Capability::ConfigAccess;
    if (name == "diagnostics" || name == "diag") return Capability::DiagnosticsAccess;
    if (name == "metadata") return Capability::MetadataAccess;
    if (name == "device.enum" || name == "device") return Capability::DeviceEnumerate;
    if (name == "device.input" || name == "input") return Capability::DeviceInput;
    if (name == "audio.out" || name == "audio") return Capability::AudioOutput;
    if (name == "audio.in") return Capability::AudioInput;
    if (name == "sensor" || name == "sensors") return Capability::SensorAccess;
    if (name == "network" || name == "net") return Capability::NetworkAccess;
    if (name == "notification" || name == "notify") return Capability::NotificationPost;
    if (name == "document.read" || name == "doc.read") return Capability::DocumentRead;
    if (name == "document.write" || name == "doc.write") return Capability::DocumentWrite;
    if (name == "document.create" || name == "doc.create") return Capability::DocumentCreate;
    if (name == "document.delete" || name == "doc.delete") return Capability::DocumentDelete;
    if (name == "document.share" || name == "doc.share") return Capability::DocumentShare;
    if (name == "document.enum" || name == "doc.enum" || name == "document" || name == "doc") return Capability::DocumentEnumerate;
    if (name == "settings.read" || name == "settings") return Capability::SettingsRead;
    if (name == "settings.write") return Capability::SettingsWrite;
    if (name == "task.manage" || name == "task") return Capability::TaskManage;
    if (name == "accessibility" || name == "a11y") return Capability::AccessibilityAccess;
    return Capability::None;
}

void CapabilityManager::bind_identity(std::uint32_t pid, const std::string& bundle_id, std::uint32_t uid) {
    std::lock_guard<std::mutex> guard(lock_);
    AppIdentity id{};
    id.pid = pid;
    id.bundle_id = bundle_id;
    id.uid = uid;
    id.security_token = ++next_token_;
    identities_[pid] = id;

    // Grant role-based baseline capabilities for known system components
    if (bundle_id == "com.winios.system" || pid == 1) {
        process_caps_[pid] = 0xFFFFFFFFu;
    } else if (bundle_id == "com.winios.settings") {
        process_caps_[pid] |= static_cast<std::uint32_t>(
            Capability::SettingsRead | Capability::SettingsWrite |
            Capability::AccessibilityAccess | Capability::DisplayWindow |
            Capability::StoragePrivate | Capability::LoggingAccess);
    } else if (bundle_id == "com.winios.taskmonitor") {
        process_caps_[pid] |= static_cast<std::uint32_t>(
            Capability::TaskManage | Capability::AccessibilityAccess |
            Capability::SettingsRead | Capability::DisplayWindow |
            Capability::StoragePrivate | Capability::LoggingAccess);
    }
}

void CapabilityManager::unbind_identity(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    identities_.erase(pid);
    process_caps_.erase(pid);
}

std::optional<AppIdentity> CapabilityManager::get_identity(std::uint32_t pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = identities_.find(pid);
    if (it != identities_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::string CapabilityManager::get_bundle_id(std::uint32_t pid) const {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = identities_.find(pid);
    if (it != identities_.end()) {
        return it->second.bundle_id;
    }
    return "";
}

void CapabilityManager::grant(std::uint32_t pid, Capability cap) {
    std::lock_guard<std::mutex> guard(lock_);
    process_caps_[pid] |= static_cast<std::uint32_t>(cap);
}

void CapabilityManager::grant_mask(std::uint32_t pid, std::uint32_t mask) {
    std::lock_guard<std::mutex> guard(lock_);
    process_caps_[pid] |= mask;
}

void CapabilityManager::grant_named(std::uint32_t pid, const std::vector<std::string>& perm_names) {
    std::lock_guard<std::mutex> guard(lock_);
    std::uint32_t mask = process_caps_[pid];
    for (const auto& name : perm_names) {
        mask |= static_cast<std::uint32_t>(parse_capability_name(name));
    }
    process_caps_[pid] = mask;
}

void CapabilityManager::revoke(std::uint32_t pid, Capability cap) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_caps_.find(pid);
    if (it != process_caps_.end()) {
        it->second &= ~static_cast<std::uint32_t>(cap);
    }
}

void CapabilityManager::revoke_all(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    process_caps_.erase(pid);
}

bool CapabilityManager::check(std::uint32_t pid, Capability cap) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    audit_stats_.total_checks++;

    auto it = process_caps_.find(pid);
    if (it == process_caps_.end()) {
        // PID 1 (Shell/System) has implicit full permissions
        if (pid == 1) {
            audit_stats_.total_grants++;
            return true;
        }
        audit_stats_.total_denials++;
        return false;
    }
    const std::uint32_t mask = it->second;
    const bool granted = (mask & static_cast<std::uint32_t>(cap)) == static_cast<std::uint32_t>(cap);
    if (granted) {
        audit_stats_.total_grants++;
    } else {
        audit_stats_.total_denials++;
    }
    return granted;
}

bool CapabilityManager::check_storage_access(std::uint32_t pid, const std::string& target_bundle_id) const noexcept {
    if (pid == 1) return true; // System process
    std::lock_guard<std::mutex> guard(lock_);
    audit_stats_.total_checks++;

    auto id_it = identities_.find(pid);
    if (id_it == identities_.end()) {
        audit_stats_.total_denials++;
        return false; // Unbound identity
    }

    auto cap_it = process_caps_.find(pid);
    const std::uint32_t mask = (cap_it != process_caps_.end()) ? cap_it->second : 0;

    if (id_it->second.bundle_id == target_bundle_id) {
        // Private access
        const bool ok = (mask & static_cast<std::uint32_t>(Capability::StoragePrivate)) != 0;
        if (ok) audit_stats_.total_grants++; else audit_stats_.total_denials++;
        return ok;
    } else {
        // Cross-application shared access
        const bool ok = (mask & static_cast<std::uint32_t>(Capability::StorageShared)) != 0;
        if (ok) audit_stats_.total_grants++; else audit_stats_.total_denials++;
        return ok;
    }
}

std::uint32_t CapabilityManager::get_mask(std::uint32_t pid) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_caps_.find(pid);
    if (it != process_caps_.end()) {
        return it->second;
    }
    return pid == 1 ? 0xFFFFFFFF : 0;
}

SecurityAuditStats CapabilityManager::get_audit_stats() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return audit_stats_;
}

void CapabilityManager::reset_audit_stats() noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    audit_stats_ = SecurityAuditStats{};
}

} // namespace winios::security
