#include "services/diagnostics_service.h"
#include "kernel/user_syscall_dispatcher.h"
#include "security/capability_manager.h"
#include "winios_sdk/winios_sdk.h"

#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;

static void test_monotonic_sequence_and_timestamps() {
    services::DiagnosticsService svc;
    TEST_ASSERT(svc.total_event_count() == 0);

    svc.record_event(1, 1, 100, 0, "TestSubsystem");
    svc.record_event(1, 2, 100, 0, "TestSubsystem");
    svc.record_event(2, 3, 101, -5, "OtherSubsystem");

    TEST_ASSERT(svc.total_event_count() == 3);

    auto evs = svc.query_events(0, 0, 0, 10, 100, true);
    TEST_ASSERT(evs.size() == 3);
    TEST_ASSERT(evs[0].sequence == 1);
    TEST_ASSERT(evs[1].sequence == 2);
    TEST_ASSERT(evs[2].sequence == 3);
    TEST_ASSERT(evs[0].timestamp_us > 0);
    TEST_ASSERT(evs[0].timestamp_us <= evs[1].timestamp_us);
    std::cout << "[PASS] test_monotonic_sequence_and_timestamps\n";
}

static void test_ring_buffer_bounded_capacity() {
    services::DiagnosticsService svc;
    for (uint32_t i = 1; i <= 600; ++i) {
        svc.record_event(1, 1, 100, 0, "Stress");
    }

    TEST_ASSERT(svc.total_event_count() == 600);

    auto evs = svc.query_events(0, 0, 0, 1000, 100, true);
    TEST_ASSERT(evs.size() <= 128);
    TEST_ASSERT(evs[0].sequence > 1);
    std::cout << "[PASS] test_ring_buffer_bounded_capacity\n";
}

static void test_category_and_severity_filtering() {
    services::DiagnosticsService svc;
    svc.record_event(1 /* Cat 1 */, 1 /* Info */, 100, 0, "Sub1");
    svc.record_event(1 /* Cat 1 */, 3 /* Error */, 100, -2, "Sub1");
    svc.record_event(2 /* Cat 2 */, 1 /* Info */, 100, 0, "Sub2");
    svc.record_event(2 /* Cat 2 */, 3 /* Error */, 100, -3, "Sub2");

    auto cat2 = svc.query_events(0, 2, 0, 10, 100, true);
    TEST_ASSERT(cat2.size() == 2);
    TEST_ASSERT(cat2[0].category == 2);
    TEST_ASSERT(cat2[1].category == 2);

    auto errs = svc.query_events(0, 0, 3, 10, 100, true);
    TEST_ASSERT(errs.size() == 2);
    TEST_ASSERT(errs[0].level == 3);
    TEST_ASSERT(errs[1].level == 3);

    auto cat1_err = svc.query_events(0, 1, 3, 10, 100, true);
    TEST_ASSERT(cat1_err.size() == 1);
    TEST_ASSERT(cat1_err[0].category == 1);
    TEST_ASSERT(cat1_err[0].level == 3);
    TEST_ASSERT(std::string(cat1_err[0].tag) == "Sub1");
    std::cout << "[PASS] test_category_and_severity_filtering\n";
}

static void test_per_application_visibility_restrictions() {
    services::DiagnosticsService svc;
    svc.record_event(1, 1, 0, 0, "SystemKernel");
    svc.record_event(1, 1, 100, 0, "AppA");
    svc.record_event(1, 1, 200, 0, "AppB");

    auto appA_events = svc.query_events(0, 0, 0, 10, 100, false /* unprivileged */);
    TEST_ASSERT(appA_events.size() == 2);
    TEST_ASSERT(appA_events[0].pid == 0);
    TEST_ASSERT(appA_events[1].pid == 100);

    auto all_events = svc.query_events(0, 0, 0, 10, 100, true /* privileged */);
    TEST_ASSERT(all_events.size() == 3);
    std::cout << "[PASS] test_per_application_visibility_restrictions\n";
}

static void test_pagination_continuation() {
    services::DiagnosticsService svc;
    for (uint32_t i = 1; i <= 20; ++i) {
        svc.record_event(1, 1, 100, 0, "Paginate");
    }

    auto p1 = svc.query_events(0, 0, 0, 5, 100, true);
    TEST_ASSERT(p1.size() == 5);
    TEST_ASSERT(p1[0].sequence == 1);
    TEST_ASSERT(p1.back().sequence == 5);

    auto p2 = svc.query_events(p1.back().sequence, 0, 0, 5, 100, true);
    TEST_ASSERT(p2.size() == 5);
    TEST_ASSERT(p2[0].sequence == 6);
    TEST_ASSERT(p2.back().sequence == 10);
    std::cout << "[PASS] test_pagination_continuation\n";
}

int main() {
    std::cout << "=== Running Stage 229 Diagnostics Query Tests ===\n";
    test_monotonic_sequence_and_timestamps();
    test_ring_buffer_bounded_capacity();
    test_category_and_severity_filtering();
    test_per_application_visibility_restrictions();
    test_pagination_continuation();
    std::cout << "All Stage 229 Diagnostics Query Tests Passed!\n";
    return 0;
}
