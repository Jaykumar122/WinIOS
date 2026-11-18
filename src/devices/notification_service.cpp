#include "devices/notification_service.h"
#include "security/capability_manager.h"

#include <chrono>
#include <cstring>
#include <algorithm>

namespace winios::devices {

VirtualNotificationService::VirtualNotificationService()
    : VirtualDeviceBase(kDefaultNotificationDeviceId,
                        1,
                        DeviceClass::Notification,
                        "WinIOS Notification Service",
                        "WinIOS Banner Subsystem",
                        0x00010900,
                        static_cast<std::uint32_t>(security::Capability::NotificationPost)) {}

std::uint32_t VirtualNotificationService::post_notification(std::uint32_t sender_pid,
                                                            const char* title,
                                                            const char* body,
                                                            const char* category,
                                                            std::uint32_t duration_ms) {
    if (!title || !body) {
        return 0;
    }

    std::lock_guard<std::mutex> guard(lock_);
    if (state_ != DeviceState::Running && state_ != DeviceState::Ready) {
        return 0;
    }

    // Maintain bounded ring capacity
    while (notifications_.size() >= kMaxActiveNotifications) {
        notifications_.pop_front();
    }

    const auto now_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    NotificationData note{};
    note.id = next_id_++;
    note.sender_pid = sender_pid;
    note.timestamp_us = now_us;
    note.duration_ms = duration_ms;
    note.flags = 0;

    strncpy_s(note.title, sizeof(note.title), title, _TRUNCATE);
    strncpy_s(note.body, sizeof(note.body), body, _TRUNCATE);
    if (category) {
        strncpy_s(note.category, sizeof(note.category), category, _TRUNCATE);
    } else {
        strncpy_s(note.category, sizeof(note.category), "default", _TRUNCATE);
    }

    notifications_.push_back(note);
    ++total_posted_;

    return note.id;
}

bool VirtualNotificationService::dismiss_notification(std::uint32_t id) {
    std::lock_guard<std::mutex> guard(lock_);
    for (auto it = notifications_.begin(); it != notifications_.end(); ++it) {
        if (it->id == id && !(it->flags & 0x1)) {
            it->flags |= 0x1; // Dismissed
            ++total_dismissed_;
            return true;
        }
    }
    return false;
}

void VirtualNotificationService::clear_all() {
    std::lock_guard<std::mutex> guard(lock_);
    notifications_.clear();
}

std::vector<NotificationData> VirtualNotificationService::get_active_notifications(std::uint64_t current_time_us) const {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<NotificationData> active;

    for (const auto& note : notifications_) {
        if (note.flags & 0x1) {
            // Dismissed
            continue;
        }
        if (note.duration_ms > 0 && current_time_us > 0) {
            const std::uint64_t expiry_us = note.timestamp_us + (static_cast<std::uint64_t>(note.duration_ms) * 1000);
            if (current_time_us >= expiry_us) {
                // Expired
                continue;
            }
        }
        active.push_back(note);
    }

    return active;
}

std::size_t VirtualNotificationService::active_count() const {
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t count = 0;
    for (const auto& note : notifications_) {
        if (!(note.flags & 0x1)) {
            ++count;
        }
    }
    return count;
}

std::uint64_t VirtualNotificationService::total_posted() const {
    std::lock_guard<std::mutex> guard(lock_);
    return total_posted_;
}

bool VirtualNotificationService::initialize() {
    std::lock_guard<std::mutex> guard(lock_);
    notifications_.clear();
    state_ = DeviceState::Ready;
    return true;
}

bool VirtualNotificationService::start() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Ready || state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

bool VirtualNotificationService::stop() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running || state_ == DeviceState::Suspended) {
        notifications_.clear();
        state_ = DeviceState::Stopped;
        return true;
    }
    return false;
}

bool VirtualNotificationService::suspend() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Running) {
        state_ = DeviceState::Suspended;
        return true;
    }
    return false;
}

bool VirtualNotificationService::resume() {
    std::lock_guard<std::mutex> guard(lock_);
    if (state_ == DeviceState::Suspended) {
        state_ = DeviceState::Running;
        return true;
    }
    return false;
}

void VirtualNotificationService::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    notifications_.clear();
    next_id_ = 1;
    total_posted_ = 0;
    total_dismissed_ = 0;
    state_ = DeviceState::Ready;
}

} // namespace winios::devices
