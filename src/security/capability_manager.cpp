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
    return Capability::None;
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

void CapabilityManager::revoke_all(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    process_caps_.erase(pid);
}

bool CapabilityManager::check(std::uint32_t pid, Capability cap) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_caps_.find(pid);
    if (it == process_caps_.end()) {
        // PID 1 (Shell/System) has implicit full permissions
        return pid == 1;
    }
    const std::uint32_t mask = it->second;
    return (mask & static_cast<std::uint32_t>(cap)) == static_cast<std::uint32_t>(cap);
}

std::uint32_t CapabilityManager::get_mask(std::uint32_t pid) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = process_caps_.find(pid);
    if (it != process_caps_.end()) {
        return it->second;
    }
    return pid == 1 ? 0xFFFFFFFF : 0;
}

} // namespace winios::security
