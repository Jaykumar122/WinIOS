// ==============================================================================
// WinIOS Campaign 198 Stage 15: Deterministic Replay with Memory, DMA & Cache
// ==============================================================================
// Verifies:
// 1. Bit-exact replay determinism across multiple execution runs
// 2. Integration of memory access, atomic RMW, DMA transfers, and cache maintenance
// 3. FNV-1a cumulative trace hash consistency for deterministic debugging
// ==============================================================================

#include "cpu/guest_memory_order.h"
#include "device/virtual_dma_engine.h"
#include "cpu/smp_shared_memory.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::device;

static constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;
static constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;

static std::uint64_t fnv1a_hash_step(std::uint64_t hash, std::uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        std::uint8_t byte = static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF);
        hash ^= byte;
        hash *= kFnvPrime;
    }
    return hash;
}

// Simulates an integrated workload with memory, DMA, and cache events for a given seed
static std::uint64_t run_integrated_simulation(std::uint32_t seed, std::size_t num_events = 300) {
    GuestMemoryOrderModel model(4);
    auto shared_mem = std::make_shared<SmpSharedMemory>(1024 * 1024, 4);
    VirtualDmaEngine dma(shared_mem);

    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::size_t> core_dist(0, 3);
    std::uniform_int_distribution<int> event_dist(0, 4);
    std::uniform_int_distribution<std::uint64_t> addr_dist(0, 31);
    std::uniform_int_distribution<std::uint64_t> val_dist(1, 0xFFFFFFFFULL);

    std::uint64_t cumulative_hash = kFnvOffsetBasis;

    for (std::size_t i = 0; i < num_events; ++i) {
        std::size_t core = core_dist(rng);
        int ev = event_dist(rng);
        std::uint64_t addr = 0x20000 + (addr_dist(rng) * 64);
        std::uint64_t val = val_dist(rng);

        switch (ev) {
            case 0: // Normal memory store & record
                shared_mem->write_u64(core, addr, val);
                model.record_write(core, addr, 8, val, MemoryAccessType::NormalWrite);
                cumulative_hash = fnv1a_hash_step(cumulative_hash, val ^ addr);
                break;

            case 1: // Normal memory load & record
                {
                    std::uint64_t r = shared_mem->read_u64(core, addr);
                    model.record_read(core, addr, 8, r, MemoryAccessType::NormalRead);
                    cumulative_hash = fnv1a_hash_step(cumulative_hash, r ^ (addr + 1));
                }
                break;

            case 2: // LSE Atomic (e.g. LDADD or SWP)
                {
                    std::uint64_t old_val = shared_mem->read_u64(core, addr);
                    shared_mem->write_u64(core, addr, old_val + val);
                    model.record_read(core, addr, 8, old_val, MemoryAccessType::NormalRead);
                    model.record_write(core, addr, 8, old_val + val, MemoryAccessType::NormalWrite);
                    cumulative_hash = fnv1a_hash_step(cumulative_hash, (old_val + val) ^ 0xAAAA5555ULL);
                }
                break;

            case 3: // DMA transfer event
                {
                    VirtualDmaDescriptor desc{};
                    desc.descriptor_id = static_cast<std::uint64_t>(i + 1);
                    desc.dst_paddr = addr;
                    desc.length_bytes = 64;
                    desc.operation_type = static_cast<std::uint32_t>(VirtualDmaOpType::Fill);
                    desc.fill_pattern = static_cast<std::uint32_t>(val);
                    shared_mem->write_bytes(0, 0x1000, &desc, sizeof(desc));
                    auto res = dma.execute_descriptor_chain(0x1000, core);
                    TEST_ASSERT(res.is_success());
                    cumulative_hash = fnv1a_hash_step(cumulative_hash, res.bytes_transferred ^ 0x5555AAAAULL);
                }
                break;

            case 4: // Cache maintenance event
                {
                    model.record_dmb(core, 11); // DMB ISH
                    cumulative_hash = fnv1a_hash_step(cumulative_hash, addr ^ 0xCAFEBABEBABEBABFULL);
                }
                break;
        }
    }

    return cumulative_hash;
}

void test_replay_determinism_across_runs() {
    std::cout << "[Test 1] Deterministic replay hash repeatability across independent runs...\n";

    const std::vector<std::uint32_t> seeds = {42, 1337, 0xCAFEBABE, 99999, 1234567};

    for (std::uint32_t s : seeds) {
        const std::uint64_t h1 = run_integrated_simulation(s, 250);
        const std::uint64_t h2 = run_integrated_simulation(s, 250);
        const std::uint64_t h3 = run_integrated_simulation(s, 250);

        TEST_ASSERT(h1 == h2);
        TEST_ASSERT(h2 == h3);
    }

    std::cout << "  -> PASSED (5 seeds tested with identical hashes across 3 runs each)\n";
}

void test_distinct_seeds_produce_distinct_traces() {
    std::cout << "[Test 2] Distinct seeds produce distinct deterministic traces...\n";
    const std::uint64_t h1 = run_integrated_simulation(101, 200);
    const std::uint64_t h2 = run_integrated_simulation(102, 200);
    const std::uint64_t h3 = run_integrated_simulation(103, 200);

    TEST_ASSERT(h1 != h2);
    TEST_ASSERT(h2 != h3);
    TEST_ASSERT(h1 != h3);

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.15: Deterministic Replay with Memory, DMA & Cache\n";
    std::cout << "========================================================\n";

    test_replay_determinism_across_runs();
    test_distinct_seeds_produce_distinct_traces();

    std::cout << "\nAll Stage 198.15 tests passed successfully.\n";
    return 0;
}
