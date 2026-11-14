// tests/unit/stage197_13_deterministic_replay_tests.cpp
// Campaign 197: Acceptance Test J
// Verifies Deterministic Weak-Memory Scheduler & Replay:
// Ensures that for any fixed seed, execution trace and FNV-1a state hashes
// are 100% bit-exact across independent replay executions.

#include <iostream>
#include <vector>
#include <cstdint>
#include <random>
#include <iomanip>
#include <sstream>
#include "cpu/guest_memory_order.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

// Helper to simulate a multi-core interleaved litmus workload given a seed
static std::uint64_t run_seeded_simulation(std::uint32_t seed, std::size_t num_events = 200) {
    GuestMemoryOrderModel model(4);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> core_dist(0, 3);
    std::uniform_int_distribution<int> op_dist(0, 3);
    std::uniform_int_distribution<std::uint64_t> addr_dist(0, 7);
    std::uniform_int_distribution<std::uint64_t> val_dist(1, 1000);

    for (std::size_t i = 0; i < num_events; ++i) {
        std::size_t core = core_dist(rng);
        int op = op_dist(rng);
        std::uint64_t addr = 0x1000 + (addr_dist(rng) * 8);
        std::uint64_t val = val_dist(rng);

        switch (op) {
            case 0: // Normal Read
                model.record_read(core, addr, 8, val, MemoryAccessType::NormalRead);
                break;
            case 1: // Normal Write
                model.record_write(core, addr, 8, val, MemoryAccessType::NormalWrite);
                break;
            case 2: // Acquire Read or Release Write
                if (val % 2 == 0) {
                    model.record_read(core, addr, 8, val, MemoryAccessType::AcquireRead);
                } else {
                    model.record_write(core, addr, 8, val, MemoryAccessType::ReleaseWrite);
                }
                break;
            case 3: // Barrier
                if (val % 3 == 0) {
                    model.record_dmb(core, 11); // DMB ISH
                } else if (val % 3 == 1) {
                    model.record_dsb(core, 15); // DSB SY
                } else {
                    model.record_isb(core, 15); // ISB SY
                }
                break;
        }
    }

    return model.compute_ordering_state_hash();
}

static bool test_deterministic_replay_across_seeds() {
    std::cout << "  Testing deterministic replay across 5 seeds with 3 replays each..." << std::endl;
    const std::vector<std::uint32_t> seeds = {1001, 1002, 1003, 1004, 1005};

    for (auto seed : seeds) {
        std::uint64_t hash1 = run_seeded_simulation(seed);
        std::uint64_t hash2 = run_seeded_simulation(seed);
        std::uint64_t hash3 = run_seeded_simulation(seed);

        std::cout << "    Seed " << seed << " -> Hash: 0x"
                  << std::hex << std::setw(16) << std::setfill('0') << hash1
                  << std::dec << std::endl;

        TEST_ASSERT(hash1 != 0, "State hash is non-zero");
        TEST_ASSERT(hash1 == hash2, "Replay 1 matches Replay 2 bit-exactly");
        TEST_ASSERT(hash2 == hash3, "Replay 2 matches Replay 3 bit-exactly");
    }

    return true;
}

static bool test_hash_sensitivity_to_event_changes() {
    std::cout << "  Testing state hash sensitivity to single-event variance..." << std::endl;
    GuestMemoryOrderModel m1(2);
    m1.record_write(0, 0x1000, 8, 42);
    m1.record_read(1, 0x1000, 8, 42);
    std::uint64_t h1 = m1.compute_ordering_state_hash();

    // Change address
    GuestMemoryOrderModel m2(2);
    m2.record_write(0, 0x1008, 8, 42);
    m2.record_read(1, 0x1000, 8, 42);
    std::uint64_t h2 = m2.compute_ordering_state_hash();
    TEST_ASSERT(h1 != h2, "Hash differs when address changes");

    // Change value
    GuestMemoryOrderModel m3(2);
    m3.record_write(0, 0x1000, 8, 43);
    m3.record_read(1, 0x1000, 8, 42);
    std::uint64_t h3 = m3.compute_ordering_state_hash();
    TEST_ASSERT(h1 != h3, "Hash differs when value changes");

    // Change memory access type (normal vs release)
    GuestMemoryOrderModel m4(2);
    m4.record_write(0, 0x1000, 8, 42, MemoryAccessType::ReleaseWrite);
    m4.record_read(1, 0x1000, 8, 42);
    std::uint64_t h4 = m4.compute_ordering_state_hash();
    TEST_ASSERT(h1 != h4, "Hash differs when access type changes");

    return true;
}

int main() {
    std::cout << "[STAGE 197.13] Running Deterministic Replay Tests (Acceptance Test J)..." << std::endl;

    if (!test_deterministic_replay_across_seeds()) {
        std::cerr << "test_deterministic_replay_across_seeds FAILED" << std::endl;
        return 1;
    }

    if (!test_hash_sensitivity_to_event_changes()) {
        std::cerr << "test_hash_sensitivity_to_event_changes FAILED" << std::endl;
        return 1;
    }

    std::cout << "[STAGE 197.13] All Deterministic Replay Tests PASSED." << std::endl;
    return 0;
}
