#include "settings/system_state_manager.h"
#include "vfs/vfs.h"
#include "security/capability_manager.h"
#include "services/diagnostics_service.h"
#include "kernel/crash_recovery.h"
#include "platform/resource_budget.h"

#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <iostream>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Assertion failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::settings;

static void test_concurrent_transactions() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    settings::SystemStateManager mgr(vfs);

    constexpr int kThreads = 4;
    constexpr int kIters = 100;
    std::atomic<int> success_count{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < kIters; ++i) {
                std::vector<std::function<void(SystemSettings&)>> tx;
                tx.push_back([=](SystemSettings& s) {
                    s.appearance.theme = (i % 2 == 0) ? settings::ThemeMode::Light : settings::ThemeMode::Dark;
                });
                tx.push_back([=](SystemSettings& s) {
                    s.appearance.accent = static_cast<settings::AccentColor>((t + i) % 8);
                    s.audio.master_volume = static_cast<uint32_t>((i * 7) % 100);
                });

                if (mgr.execute_transaction(tx)) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(success_count.load() > 0);
    TEST_ASSERT(mgr.get_version() > 0);

    auto app = mgr.get_appearance();
    TEST_ASSERT(app.theme == settings::ThemeMode::Light || app.theme == settings::ThemeMode::Dark);
    TEST_ASSERT(static_cast<uint32_t>(app.accent) <= 7u);
    std::cout << "[PASS] test_concurrent_transactions\n";
}

static void test_corrupted_persistence_recovery() {
    auto vfs = std::make_shared<vfs::VirtualFileSystem>();
    vfs->mkdir("/System");
    vfs->mkdir("/System/Library");
    vfs->mkdir("/System/Library/Preferences");

    std::string garbage(4096, '\xFF');
    garbage[0] = '[';
    garbage[1] = 'G';
    garbage[2] = 'A';
    garbage[3] = 'R';
    garbage[4] = ']';
    vfs->write_file("/System/Library/Preferences/system_settings.ini", garbage.data(), garbage.size());

    settings::SystemStateManager mgr(vfs);
    TEST_ASSERT(mgr.load_from_vfs());

    auto app = mgr.get_appearance();
    TEST_ASSERT(app.theme == settings::ThemeMode::Dark || app.theme == settings::ThemeMode::System);
    std::cout << "[PASS] test_corrupted_persistence_recovery\n";
}

static void test_concurrent_diagnostics_ring_buffer() {
    services::DiagnosticsService svc;
    constexpr int kThreads = 4;
    constexpr int kIters = 200;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 0; i < kIters; ++i) {
                if (i % 3 == 0) {
                    auto evs = svc.query_events(0, 0, 0, 32, 100 + t, true);
                    (void)evs;
                } else {
                    svc.record_event(1, 1, 100 + t, 0, "StressTag");
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(svc.total_event_count() > 0);
    std::cout << "[PASS] test_concurrent_diagnostics_ring_buffer\n";
}

static void test_resource_budget_concurrency() {
    platform::ResourceBudgetManager budget;
    constexpr int kThreads = 4;
    constexpr int kIters = 100;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            const uint32_t pid = 100 + t;
            for (int i = 0; i < kIters; ++i) {
                auto err = budget.allocate_memory(pid, 1024 * 64);
                if (err.is_ok()) {
                    budget.free_memory(pid, 1024 * 64);
                }
                auto werr = budget.allocate_window(pid);
                if (werr.is_ok()) {
                    budget.release_window(pid);
                }
            }
            budget.release_all_task_resources(pid);
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    for (int t = 0; t < kThreads; ++t) {
        TEST_ASSERT(budget.get_task_memory(100 + t) == 0);
        TEST_ASSERT(budget.get_task_windows(100 + t) == 0);
    }
    std::cout << "[PASS] test_resource_budget_concurrency\n";
}

int main() {
    std::cout << "=== Running Stage 229 Adversarial & Concurrency Tests ===\n";
    test_concurrent_transactions();
    test_corrupted_persistence_recovery();
    test_concurrent_diagnostics_ring_buffer();
    test_resource_budget_concurrency();
    std::cout << "All Stage 229 Adversarial & Concurrency Tests Passed!\n";
    return 0;
}
