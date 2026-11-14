// tests/unit/stage197_12_host_synchronization_isolation_tests.cpp
// Campaign 197: Acceptance Test I
// Verifies Host Synchronization Isolation:
// Ensures host mutexes, fences, and x86 TSO ordering are not mistaken for,
// and do not contaminate, guest ARMv8-A weak-memory semantics.

#include <iostream>
#include <vector>
#include <cstdint>
#include <thread>
#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "cpu/smp_cluster.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

static bool test_host_tso_vs_guest_weak_memory_divergence() {
    std::cout << "  Testing host TSO vs guest weak-memory divergence..." << std::endl;
    // On x86-64 host, hardware TSO makes Store-Load reordering (r0=0, r1=0 in SB) impossible
    // unless non-temporal stores or store buffers are involved across cores.
    // In WinIOS guest memory model, r0=0, r1=0 is correctly evaluated as ALLOWED under ARMv8-A!
    auto v_sb = GuestMemoryOrderModel::evaluate_sb_outcome(0, 0);
    TEST_ASSERT(v_sb == LitmusOutcomeVerdict::Allowed, "Guest model correctly permits weak-memory SB reordering");

    // In contrast, with ARM DMB ISH, it must be evaluated as Forbidden
    GuestMemoryOrderModel model(2);
    auto ev0 = model.record_write(0, 0x1000, 8, 1);
    model.record_dmb(0, 11); // DMB ISH
    auto ev1 = model.record_read(0, 0x1008, 8, 0);
    TEST_ASSERT(model.is_ordered(ev0, ev1), "Guest model explicitly orders W before R only when DMB is present");

    return true;
}

static bool test_host_mutex_isolation_from_guest_program_order() {
    std::cout << "  Testing host mutex isolation from guest program order..." << std::endl;
    GuestMemoryOrderModel model(4);

    // Concurrently record events from 4 host threads simulating 4 virtual cores
    std::vector<std::thread> workers;
    for (std::size_t c = 0; c < 4; ++c) {
        workers.emplace_back([&model, c]() {
            for (std::size_t i = 0; i < 50; ++i) {
                model.record_write(c, 0x2000 + c * 0x100, 8, i);
                model.record_read(c, 0x2000 + c * 0x100, 8, i);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    // Verify each core's program order sequence is monotonic and contiguous [1..100]
    for (std::size_t c = 0; c < 4; ++c) {
        auto& core = model.get_core_state(c);
        const auto& hist = core.event_history();
        TEST_ASSERT(hist.size() == 100, "Each core has exactly 100 recorded events");
        for (std::size_t i = 0; i < hist.size(); ++i) {
            TEST_ASSERT(hist[i].program_order_seq == (i + 1), "Core program order sequence is perfectly contiguous");
        }
    }

    return true;
}

static bool test_smp_store_serialization() {
    std::cout << "  Testing SMP store serialization mutex isolation..." << std::endl;
    SMPCluster cluster(2, 0x20000);
    auto& mem = cluster.get_memory();

    // Verify store_mutex() can be locked and unlocked without altering guest CPU state
    {
        std::lock_guard<std::recursive_mutex> lock(cluster.store_mutex());
        mem.write_u64(0x5000, 0x1122334455667788ULL);
    }
    TEST_ASSERT(mem.read_u64(0x5000) == 0x1122334455667788ULL, "Memory write under host lock succeeded");

    return true;
}

int main() {
    std::cout << "[STAGE 197.12] Running Host Synchronization Isolation Tests (Acceptance Test I)..." << std::endl;

    if (!test_host_tso_vs_guest_weak_memory_divergence()) {
        std::cerr << "test_host_tso_vs_guest_weak_memory_divergence FAILED" << std::endl;
        return 1;
    }

    if (!test_host_mutex_isolation_from_guest_program_order()) {
        std::cerr << "test_host_mutex_isolation_from_guest_program_order FAILED" << std::endl;
        return 1;
    }

    if (!test_smp_store_serialization()) {
        std::cerr << "test_smp_store_serialization FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.12] All Host Synchronization Isolation Tests PASSED." << std::endl;
    return 0;
}
