#include <iostream>
#include <cstdint>
#include <thread>
#include <chrono>

#include "devices/clock_service.h"
#include "devices/lifecycle_service.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::devices;

int main() {
    std::cout << "[Test Stage 225.16] Virtual Clock, Timers & Power Lifecycle...\n" << std::flush;

    // 1. Clock Service
    VirtualClockService clock;
    TEST_ASSERT(clock.initialize());
    TEST_ASSERT(clock.start());

    uint64_t m1 = clock.now_monotonic_us();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    uint64_t m2 = clock.now_monotonic_us();
    TEST_ASSERT(m2 >= m1); // Monotonic non-decreasing

    auto spec = clock.get_time_spec();
    TEST_ASSERT(spec.monotonic_us > 0);
    TEST_ASSERT(spec.wall_time_us > 0);
    TEST_ASSERT(spec.timer_frequency_hz == 1000000);

    // 2. Virtual Timers
    // Schedule a one-shot timer for +10000 us (10ms)
    uint32_t t1 = clock.create_timer(10000, 0);
    TEST_ASSERT(t1 > 0);

    // Schedule a periodic timer for +5000 us, interval 5000 us
    uint32_t t2 = clock.create_timer(5000, 5000);
    TEST_ASSERT(t2 > 0);

    // Polling before deadline should return no expired timers
    auto expired = clock.poll_expired_timers();
    // Advance virtual time by 6000 us -> t2 should trigger
    clock.advance_virtual_time(6000);
    expired = clock.poll_expired_timers();
    TEST_ASSERT(!expired.empty());
    bool found_t2 = false;
    for (auto id : expired) {
        if (id == t2) found_t2 = true;
    }
    TEST_ASSERT(found_t2);

    // Cancel t1
    TEST_ASSERT(clock.cancel_timer(t1));

    // 3. Power & System Lifecycle Service
    VirtualLifecycleService pwr;
    TEST_ASSERT(pwr.initialize());
    TEST_ASSERT(pwr.start());

    auto pst = pwr.get_power_status();
    TEST_ASSERT(pst.battery_level == 100);
    TEST_ASSERT(pst.is_charging == 1);
    TEST_ASSERT(pst.system_power_state == 0); // Normal

    // Broadcast test
    bool event_received = false;
    SystemLifecycleEvent received_event = SystemLifecycleEvent::Foreground;
    pwr.add_event_listener([&](SystemLifecycleEvent ev) {
        event_received = true;
        received_event = ev;
    });

    pwr.broadcast_event(SystemLifecycleEvent::LowMemory);
    TEST_ASSERT(event_received);
    TEST_ASSERT(received_event == SystemLifecycleEvent::LowMemory);

    // Low battery triggering event
    event_received = false;
    pwr.set_battery_level(15);
    pst = pwr.get_power_status();
    TEST_ASSERT(pst.battery_level == 15);
    TEST_ASSERT(pst.system_power_state == 1); // LowPower
    TEST_ASSERT(event_received);
    TEST_ASSERT(received_event == SystemLifecycleEvent::LowBattery);

    std::cout << "[Test Stage 225.16] PASS: Clock & Lifecycle verified.\n";
    return 0;
}
