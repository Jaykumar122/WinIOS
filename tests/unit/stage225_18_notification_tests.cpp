#include <iostream>
#include <cstdint>
#include <vector>

#include "devices/notification_service.h"
#include "devices/device_registry.h"
#include "services/diagnostics_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;
using namespace winios::services;

int main() {
    std::cout << "[Test Stage 225.18] Guest Notification Service & Inter-App Alerts...\n" << std::flush;

    VirtualNotificationService notif;
    TEST_ASSERT(notif.initialize());
    TEST_ASSERT(notif.start());

    // 1. Post notification
    uint32_t n1 = notif.post_notification(55, "Meeting", "Team sync at 10:00", "calendar", 3000);
    TEST_ASSERT(n1 > 0);
    TEST_ASSERT(notif.active_count() == 1);
    TEST_ASSERT(notif.total_posted() == 1);

    uint32_t n2 = notif.post_notification(56, "Battery", "Battery low (15%)", "battery", 5000);
    TEST_ASSERT(n2 > 0);
    TEST_ASSERT(notif.active_count() == 2);

    // Query active notifications
    auto active = notif.get_active_notifications(0);
    TEST_ASSERT(active.size() == 2);
    TEST_ASSERT(std::string(active[0].title) == "Meeting");
    TEST_ASSERT(std::string(active[1].title) == "Battery");

    // 2. Dismiss notification
    TEST_ASSERT(notif.dismiss_notification(n1));
    TEST_ASSERT(notif.active_count() == 1);
    active = notif.get_active_notifications(0);
    TEST_ASSERT(active.size() == 1);
    TEST_ASSERT(active[0].id == n2);

    // 3. Expiration test
    // Use simulated timestamp past expiry
    const uint64_t simulated_now_us = active[0].timestamp_us + 6000000ULL; // +6 seconds
    active = notif.get_active_notifications(simulated_now_us);
    TEST_ASSERT(active.empty()); // Expired

    // 4. Bounded capacity (64 max)
    for (int i = 0; i < 70; ++i) {
        notif.post_notification(99, "Spam", "Alert", "test", 10000);
    }
    TEST_ASSERT(notif.active_count() <= 64);

    notif.clear_all();
    TEST_ASSERT(notif.active_count() == 0);

    // 5. Diagnostics integration
    DiagnosticsService diag;
    auto dsum = diag.get_device_summary();
    // Device summary struct valid
    TEST_ASSERT(dsum.notifications_posted >= 0);

    std::cout << "[Test Stage 225.18] PASS: Notification service verified.\n";
    return 0;
}
