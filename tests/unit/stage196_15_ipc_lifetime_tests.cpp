#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <random>

#include "kernel/handle_table.h"
#include "kernel/ipc.h"
#include "kernel/scheduler.h"
#include "kernel/thread.h"
#include "memory/memory.h"

using namespace winios;
using namespace winios::kernel;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test M1: HandleTable Concurrency, Attenuation & Stale Handle Rejection (Acceptance Test M)
// -----------------------------------------------------------------------------
static bool test_handle_table_concurrency_and_stale_handles() {
    HandleTable table(256);
    auto obj = std::make_shared<KernelObject>(KernelObjectType::IpcPort, 101);

    // Initial handle allocation
    const std::int32_t root_handle = table.allocate_handle(obj, HandleRights::All);
    TEST_ASSERT(root_handle > 0, "Root handle allocated");
    TEST_ASSERT(table.is_valid_handle(root_handle), "Root handle is valid");
    TEST_ASSERT(table.get_object(root_handle, HandleRights::Read) == obj, "Read right permitted");

    // Close handle and verify stale handle rejection
    TEST_ASSERT(table.close_handle(root_handle), "Handle closed");
    TEST_ASSERT(!table.is_valid_handle(root_handle), "Stale handle recognized as invalid");
    TEST_ASSERT(table.get_object(root_handle) == nullptr, "Stale handle returns nullptr");
    TEST_ASSERT(!table.close_handle(root_handle), "Double close on stale handle rejected");

    // Concurrent handle allocation, duplication, rights attenuation, and closure
    constexpr int kNumThreads = 8;
    constexpr int kOpsPerThread = 100;
    std::atomic<bool> failed{false};
    std::atomic<int> start_barrier{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            start_barrier.fetch_add(1);
            while (start_barrier.load() < kNumThreads) {
                std::this_thread::yield();
            }

            auto thread_obj = std::make_shared<KernelObject>(KernelObjectType::Thread, 1000 + t);
            std::mt19937 rng(999 + t);
            std::vector<std::int32_t> local_handles;

            for (int op = 0; op < kOpsPerThread; ++op) {
                const int act = rng() % 3;
                if (act == 0 || local_handles.empty()) {
                    // Allocate new handle
                    const std::int32_t h = table.allocate_handle(thread_obj, HandleRights::All);
                    if (h > 0) {
                        local_handles.push_back(h);
                    }
                } else if (act == 1) {
                    // Duplicate handle with rights attenuation (remove Destroy right)
                    const std::size_t idx = rng() % local_handles.size();
                    const std::int32_t src_h = local_handles[idx];
                    const std::int32_t dup_h = table.duplicate_handle(src_h, HandleRights::Read | HandleRights::Write);
                    if (dup_h > 0) {
                        // Assert attenuated handle does NOT have destroy right
                        if (table.get_object(dup_h, HandleRights::Destroy) != nullptr) {
                            failed.store(true);
                            return;
                        }
                        local_handles.push_back(dup_h);
                    }
                } else {
                    // Close handle
                    const std::size_t idx = rng() % local_handles.size();
                    const std::int32_t h = local_handles[idx];
                    if (!table.close_handle(h)) {
                        failed.store(true);
                        return;
                    }
                    local_handles.erase(local_handles.begin() + idx);
                }
            }

            // Cleanup remaining
            for (auto h : local_handles) {
                if (!table.close_handle(h)) {
                    failed.store(true);
                    return;
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(!failed.load(), "Concurrent handle table operations succeeded");
    TEST_ASSERT(table.active_handles_count() == 0, "Zero active handles remain in table");

    return true;
}

// -----------------------------------------------------------------------------
// Test M2: IpcManager Port Concurrency, Messaging & Destruction (Acceptance Test M)
// -----------------------------------------------------------------------------
static bool test_ipc_port_concurrency_and_destruction() {
    IpcManager mgr;
    Memory mem(1024 * 1024);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);

    // Create shared port with capacity 16
    IpcPort* port = mgr.create_port(1, 16);
    TEST_ASSERT(port != nullptr, "IPC port created");
    const std::uint32_t port_id = port->port_id();

    constexpr int kNumProducers = 4;
    constexpr int kNumConsumers = 4;
    constexpr int kMsgsPerProducer = 100;
    std::atomic<bool> failed{false};
    std::atomic<int> start_barrier{0};
    std::atomic<std::size_t> msgs_sent{0};
    std::atomic<std::size_t> msgs_received{0};

    std::vector<std::thread> threads;

    // Launch producers
    for (int p = 0; p < kNumProducers; ++p) {
        threads.emplace_back([&, p]() {
            start_barrier.fetch_add(1);
            while (start_barrier.load() < (kNumProducers + kNumConsumers)) {
                std::this_thread::yield();
            }

            for (int m = 0; m < kMsgsPerProducer; ++m) {
                KernelMessage msg{};
                msg.sender_tid = 100 + p;
                msg.receiver_tid = 200;
                msg.msg_id = static_cast<std::uint32_t>(m);
                msg.payload_size = 4;
                msg.payload[0] = static_cast<std::uint8_t>(p);

                while (!port->try_send(msg, sched)) {
                    std::this_thread::yield();
                }
                msgs_sent.fetch_add(1);
            }
        });
    }

    // Launch consumers
    for (int c = 0; c < kNumConsumers; ++c) {
        threads.emplace_back([&, c]() {
            start_barrier.fetch_add(1);
            while (start_barrier.load() < (kNumProducers + kNumConsumers)) {
                std::this_thread::yield();
            }

            while (msgs_received.load() < (kNumProducers * kMsgsPerProducer)) {
                KernelMessage out_msg{};
                if (port->try_recv(out_msg, sched)) {
                    msgs_received.fetch_add(1);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    TEST_ASSERT(msgs_sent.load() == kNumProducers * kMsgsPerProducer, "All messages sent");
    TEST_ASSERT(msgs_received.load() == msgs_sent.load(), "All messages received");
    TEST_ASSERT(port->is_empty(), "Port is empty after test");

    // Test port destruction and stale lookup
    TEST_ASSERT(mgr.destroy_port(port_id, sched), "Port destroyed");
    TEST_ASSERT(mgr.get_port(port_id) == nullptr, "Destroyed port returns nullptr on lookup");
    TEST_ASSERT(!mgr.destroy_port(port_id, sched), "Double destroy rejected");

    return true;
}

int main() {
    std::cout << "[STAGE 196.15] Running Kernel IPC & Object Lifetime Tests (Acceptance Test M)...\n";

    if (!test_handle_table_concurrency_and_stale_handles()) return 1;
    if (!test_ipc_port_concurrency_and_destruction()) return 1;

    std::cout << "[STAGE 196.15] All Kernel IPC & Object Lifetime Tests PASSED (Acceptance Test M VERIFIED).\n";
    return 0;
}
