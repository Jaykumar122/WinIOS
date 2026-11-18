#pragma once

#include "devices/virtual_device_interface.h"

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>

namespace winios::devices {

#pragma pack(push, 8)
struct NotificationData {
    std::uint32_t id{0};
    std::uint32_t sender_pid{0};
    char          title[64]{0};
    char          body[128]{0};
    char          category[32]{0};
    std::uint64_t timestamp_us{0};
    std::uint32_t duration_ms{3000};
    std::uint32_t flags{0}; // Bit 0 = Dismissed, Bit 1 = Expired
};
#pragma pack(pop)

class VirtualNotificationService : public VirtualDeviceBase {
public:
    static constexpr std::uint32_t kDefaultNotificationDeviceId = 9;
    static constexpr std::size_t   kMaxActiveNotifications     = 64;

    VirtualNotificationService();
    ~VirtualNotificationService() override = default;

    // Post & Dismiss
    std::uint32_t post_notification(std::uint32_t sender_pid,
                                    const char* title,
                                    const char* body,
                                    const char* category = "system",
                                    std::uint32_t duration_ms = 3000);
    bool dismiss_notification(std::uint32_t id);
    void clear_all();

    // Query active notifications
    [[nodiscard]] std::vector<NotificationData> get_active_notifications(std::uint64_t current_time_us) const;
    [[nodiscard]] std::size_t active_count() const;
    [[nodiscard]] std::uint64_t total_posted() const;

    // Lifecycle
    bool initialize() override;
    bool start() override;
    bool stop() override;
    bool suspend() override;
    bool resume() override;
    void reset() override;

private:
    mutable std::mutex lock_;
    std::uint32_t      next_id_{1};
    std::deque<NotificationData> notifications_;
    std::uint64_t      total_posted_{0};
    std::uint64_t      total_dismissed_{0};
};

} // namespace winios::devices
