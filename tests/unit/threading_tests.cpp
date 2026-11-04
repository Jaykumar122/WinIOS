// Phase 43: concurrency, parallel-execution, and synchronization tests.
//
// These tests exercise the optional parallel engine and the synchronization
// primitives that make it safe. They are intentionally written against guest
// programs where the expected architectural result is exact, so a missing lock
// shows up as a wrong value rather than merely a slower run.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "cpu/smp_cluster.h"
#include "memory/memory.h"
#include "test_harness.h"

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;
constexpr std::uint32_t kWfi = 0xD503207Fu;
constexpr std::uint32_t kWfe = 0xD503205Fu;
constexpr std::uint32_t kSev = 0xD503209Fu;
constexpr std::uint32_t kLdxrX0X1 = 0xC85F7C20u; // LDXR X0, [X1]
constexpr std::uint32_t kAddX0X0One = 0x91000400u; // ADD X0, X0, #1
constexpr std::uint32_t kStxrW2X0X1 = 0xC8027C20u; // STXR W2, X0, [X1]
constexpr std::uint32_t kLdrX1X2 = 0xF9400041u; // LDR X1, [X2]
constexpr std::uint32_t kBranchSelf = 0x14000000u; // B .
constexpr std::uint32_t kBranchBack4 = 0x17FFFFFFu; // B .-4

std::uint32_t movz_x(unsigned rd, std::uint16_t imm16) {
    return 0xD2800000u | (static_cast<std::uint32_t>(imm16) << 5u) | rd;
}

std::uint32_t ldadd_x(unsigned rs, unsigned rt, unsigned rn) {
    return 0xF8200000u | (rs << 16u) | (rn << 5u) | rt;
}

std::uint32_t subs_x_imm(unsigned rd, unsigned rn, unsigned imm12) {
    return 0xF1000000u | ((imm12 & 0xFFFu) << 10u) | (rn << 5u) | rd;
}

std::uint32_t cbnz_x(unsigned rt, std::int64_t byte_offset) {
    const std::int64_t imm19 = byte_offset / 4;
    return 0xB5000000u
         | ((static_cast<std::uint32_t>(imm19) & 0x7FFFFu) << 5u)
         | rt;
}

// ---------------------------------------------------------------------------
// Memory-level tests
// ---------------------------------------------------------------------------

void memory_concurrent_disjoint_writes() {
    Memory memory(64 * 1024);
    memory.set_concurrent_mode(true);

    constexpr std::size_t kThreads = 4;
    constexpr std::size_t kPerThread = 2000;
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < kThreads; ++t) {
        threads.emplace_back([&memory, t] {
            const std::size_t base = t * 4096u;
            for (std::size_t i = 0; i < kPerThread; ++i) {
                const std::size_t offset = base + (i % 256u) * 8u;
                memory.write_u64(offset, (static_cast<std::uint64_t>(t) << 32u) | i);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    for (std::size_t t = 0; t < kThreads; ++t) {
        const std::size_t base = t * 4096u;
        for (std::size_t i = 0; i < 256u; ++i) {
            const std::size_t offset = base + i * 8u;
            const std::uint64_t value = memory.read_u64(offset);
            CHECK((value >> 32u) == t);
            CHECK((value & 0xFFFFFFFFu) < kPerThread);
        }
    }
}

void memory_concurrent_torn_read_check() {
    Memory memory(4096);
    memory.set_concurrent_mode(true);

    constexpr std::uint64_t kPatternA = 0xAAAAAAAAAAAAAAAAull;
    constexpr std::uint64_t kPatternB = 0x5555555555555555ull;
    memory.write_u64(0, kPatternB);
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> torn{0};

    std::thread writer([&memory, &stop] {
        for (std::size_t i = 0; i < 200000u; ++i) {
            memory.write_u64(0, (i & 1u) ? kPatternA : kPatternB);
        }
        stop.store(true, std::memory_order_release);
    });
    std::thread reader([&memory, &stop, &torn] {
        while (!stop.load(std::memory_order_acquire)) {
            const std::uint64_t value = memory.read_u64(0);
            if (value != kPatternA && value != kPatternB) {
                torn.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });
    writer.join();
    reader.join();

    CHECK(torn.load() == 0);
}

void memory_store_observer_invalidates_reservation() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.set_concurrent_mode(true);

    cluster.register_exclusive_reservation(0, 0x100, 8);
    memory.write_u64(0x100, 0x1234); // overlapping store
    CHECK(!cluster.check_and_clear_exclusive_reservation(0, 0x100, 8));

    cluster.register_exclusive_reservation(0, 0x200, 8);
    memory.write_u64(0x300, 0x5678); // disjoint store
    CHECK(cluster.check_and_clear_exclusive_reservation(0, 0x200, 8));

    memory.set_concurrent_mode(false);
}

// Four host threads drive per-core PPI lines and evaluation while a fifth
// thread rewrites distributor state through MMIO. Every path takes the GIC
// mutex; the test asserts that no cross-core access produces an interrupt ID
// outside the expected PPI set and that the distributor is still usable after
// the storm.
void gic_concurrent_mmio_and_evaluation() {
    winios::GICv3 gic;
    gic.set_core_count(4);
    gic.reset();

    for (std::size_t c = 0; c < 4; ++c) {
        gic.write_icc_pmr(0xF0u, c);
        gic.write_icc_igrpen0(1u, c);
        gic.write_icc_igrpen1(1u, c);
    }
    // Put SPI 40 in Group 1 (GICD_IGROUPR word 1) and enable it
    // (GICD_ISENABLER word 1, bit 8).
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0084u, 1u << 8u, 4);
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0104u, 1u << 8u, 4);

    std::atomic<std::size_t> anomalies{0};
    std::vector<std::thread> threads;
    for (std::size_t c = 0; c < 4; ++c) {
        threads.emplace_back([&, c] {
            for (std::size_t i = 0; i < 3000u; ++i) {
                gic.set_ppi_level(30u, (i & 1u) != 0u, c);
                const auto sig = gic.evaluate(1u, 0u, c);
                if (sig.has_interrupt && sig.intid != 30u
                    && sig.intid != winios::GICv3::kSpuriousIntId) {
                    anomalies.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    threads.emplace_back([&] {
        for (std::size_t i = 0; i < 3000u; ++i) {
            gic.write_mmio(winios::GICv3::kGicdBase + 0x0000u,
                           (i & 1u) ? 3u : 0u, 4);
            (void)gic.read_mmio(winios::GICv3::kGicdBase + 0x0000u, 4);
            gic.write_mmio(winios::GICv3::kGicdBase + 0x0104u, 1u << 8u, 4);
        }
    });
    for (auto& thread : threads) {
        thread.join();
    }

    CHECK(anomalies.load() == 0);

    // Leave a deterministic final state: no PPI level, distributor enabled,
    // SPI 40 pending. A correct post-storm evaluation must report INTID 40.
    for (std::size_t c = 0; c < 4; ++c) {
        gic.set_ppi_level(30u, false, c);
    }
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0000u, 3u, 4);
    gic.write_mmio(winios::GICv3::kGicdBase + 0x0104u, 1u << 8u, 4);
    gic.set_spi_level(40u, true);
    const auto sig = gic.evaluate(1u, 0u, 0);
    CHECK(sig.has_interrupt);
    CHECK(sig.intid == 40u);
    gic.set_spi_level(40u, false);
}

// ---------------------------------------------------------------------------
// Guest-program helpers
// ---------------------------------------------------------------------------

void install_lse_counter_program(Memory& memory, std::size_t base) {
    memory.write_u32(base + 0u, ldadd_x(1, 0, 2));       // LDADD X1, X0, [X2]
    memory.write_u32(base + 4u, subs_x_imm(3, 3, 1));    // SUBS X3, X3, #1
    memory.write_u32(base + 8u, cbnz_x(3, -8));          // CBNZ X3, base
    memory.write_u32(base + 12u, kWfi);                  // WFI
}

void install_exclusive_counter_program(Memory& memory, std::size_t base) {
    memory.write_u32(base + 0u, kLdxrX0X1);              // LDXR X0, [X1]
    memory.write_u32(base + 4u, kAddX0X0One);            // ADD X0, X0, #1
    memory.write_u32(base + 8u, kStxrW2X0X1);            // STXR W2, X0, [X1]
    memory.write_u32(base + 12u, cbnz_x(2, -12));        // CBNZ W2, base
    memory.write_u32(base + 16u, subs_x_imm(3, 3, 1));   // SUBS X3, X3, #1
    memory.write_u32(base + 20u, cbnz_x(3, -20));        // CBNZ X3, base
    memory.write_u32(base + 24u, kWfi);                  // WFI
}

void prepare_lse_cores(winios::SMPCluster& cluster, std::size_t entry,
                       std::uint64_t counter_address, std::uint64_t iterations) {
    cluster.initialize_core(0, entry);
    cluster.start_secondary_core(1, entry);
    for (std::size_t i = 0; i < 2; ++i) {
        CPU& core = cluster.get_core(i);
        core.set_architectural_exceptions(true);
        core.write_register(1, 1); // increment
        core.write_register(2, counter_address);
        core.write_register(3, iterations);
    }
}

void prepare_exclusive_cores(winios::SMPCluster& cluster, std::size_t entry,
                             std::uint64_t counter_address,
                             std::uint64_t iterations) {
    cluster.initialize_core(0, entry);
    cluster.start_secondary_core(1, entry);
    for (std::size_t i = 0; i < 2; ++i) {
        CPU& core = cluster.get_core(i);
        core.set_architectural_exceptions(true);
        core.write_register(1, counter_address);
        core.write_register(3, iterations);
    }
}

// ---------------------------------------------------------------------------
// Parallel-engine tests
// ---------------------------------------------------------------------------

void parallel_lse_atomic_counter() {
    constexpr std::uint64_t kIterations = 2000;
    constexpr std::uint64_t kCounter = 0x4000;

    for (int repetition = 0; repetition < 5; ++repetition) {
        winios::SMPCluster cluster(2, 0x20000);
        Memory& memory = cluster.get_memory();
        install_lse_counter_program(memory, 0x1000);
        memory.write_u64(kCounter, 0);
        prepare_lse_cores(cluster, 0x1000, kCounter, kIterations);

        const std::size_t retired = cluster.run_parallel(0);
        CHECK(retired > 0);
        CHECK(memory.read_u64(kCounter) == kIterations * 2);
        CHECK(cluster.shared_counter_load() == retired);
        CHECK(cluster.get_core(0).get_guest_counter()
              == cluster.get_core(1).get_guest_counter());
        cluster.stop_parallel();
    }
}

void parallel_exclusive_counter() {
    constexpr std::uint64_t kIterations = 1500;
    constexpr std::uint64_t kCounter = 0x5000;

    for (int repetition = 0; repetition < 5; ++repetition) {
        winios::SMPCluster cluster(2, 0x20000);
        Memory& memory = cluster.get_memory();
        install_exclusive_counter_program(memory, 0x2000);
        memory.write_u64(kCounter, 0);
        prepare_exclusive_cores(cluster, 0x2000, kCounter, kIterations);

        const std::size_t retired = cluster.run_parallel(0);
        CHECK(retired > 0);
        CHECK(memory.read_u64(kCounter) == kIterations * 2);
        cluster.stop_parallel();
    }
}

void parallel_sev_wfe_wakeup() {
    for (int repetition = 0; repetition < 20; ++repetition) {
        winios::SMPCluster cluster(2, 0x10000);
        Memory& memory = cluster.get_memory();
        memory.write_u32(0x3000, kWfe);
        memory.write_u32(0x3004, kAddX0X0One);
        memory.write_u32(0x3008, kWfi);
        memory.write_u32(0x3100, kSev);
        memory.write_u32(0x3104, kWfi);

        cluster.initialize_core(0, 0x3100);
        cluster.start_secondary_core(1, 0x3000);
        cluster.get_core(0).set_architectural_exceptions(true);
        cluster.get_core(1).set_architectural_exceptions(true);

        cluster.run_parallel(0);
        CHECK(cluster.get_core(1).read_register(0) == 1);
        CHECK(cluster.get_core(1).is_waiting());
        cluster.stop_parallel();
    }
}

void parallel_halted_core_does_not_execute() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x1000, kWfi);
    memory.write_u32(0x2000, kNop);
    memory.write_u32(0x2004, kWfi);

    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x2000);
    cluster.get_core(0).set_architectural_exceptions(true);
    cluster.get_core(1).set_architectural_exceptions(true);
    cluster.halt_core(1);

    const std::size_t retired = cluster.run_parallel(0);
    CHECK(retired == 1);
    CHECK(cluster.get_core(1).get_program_counter() == 0x2000);
    cluster.stop_parallel();
}

void parallel_budget_stops_execution() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x1000, kBranchSelf);
    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x1000);

    constexpr std::size_t kBudget = 2000;
    const std::size_t retired = cluster.run_parallel(kBudget);
    CHECK(retired >= kBudget);
    CHECK(retired <= kBudget + cluster.core_count());
    CHECK(cluster.shared_counter_load() == retired);
    // A second run continues from the same shared clock.
    const std::size_t retired2 = cluster.run_parallel(kBudget);
    CHECK(retired2 >= kBudget);
    CHECK(cluster.shared_counter_load() == retired + retired2);
    cluster.stop_parallel();
}

void parallel_exception_propagates() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x4000, 0xFFFFFFFFu); // unsupported encoding
    cluster.initialize_core(0, 0x4000);
    cluster.halt_core(1);

    bool threw = false;
    try {
        cluster.run_parallel(10);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(cluster.worker_exception_count() >= 1);
    cluster.stop_parallel();
}

void parallel_pause_resume() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x1000, kWfi);
    cluster.initialize_core(0, 0x1000);
    cluster.halt_core(1);
    cluster.get_core(0).set_architectural_exceptions(true);

    cluster.start_parallel();
    cluster.pause_parallel();
    CHECK(cluster.parallel_paused());
    CHECK(cluster.run_parallel(10) == 0);

    cluster.resume_parallel();
    CHECK(!cluster.parallel_paused());
    CHECK(cluster.run_parallel(10) == 1);
    cluster.stop_parallel();
}

void parallel_shared_clock_is_cluster_wide() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x1000, kBranchSelf);
    cluster.initialize_core(0, 0x1000);
    cluster.start_secondary_core(1, 0x1000);

    const std::size_t retired = cluster.run_parallel(500);
    CHECK(cluster.get_core(0).get_guest_counter() == retired);
    CHECK(cluster.get_core(1).get_guest_counter() == retired);

    cluster.get_core(0).set_guest_counter(42);
    CHECK(cluster.get_core(1).get_guest_counter() == 42);
    cluster.stop_parallel();
}

void deterministic_mode_is_repeatable() {
    constexpr std::uint64_t kIterations = 400;
    constexpr std::uint64_t kCounter = 0x4000;

    auto run_once = [](std::uint64_t counter) {
        winios::SMPCluster cluster(2, 0x20000);
        Memory& memory = cluster.get_memory();
        install_lse_counter_program(memory, 0x1000);
        memory.write_u64(counter, 0);
        prepare_lse_cores(cluster, 0x1000, counter, kIterations);
        cluster.run_deterministic(100000);
        return std::pair<std::uint64_t, std::uint64_t>{
            memory.read_u64(counter),
            cluster.get_core(0).get_program_counter()};
    };

    const auto first = run_once(kCounter);
    const auto second = run_once(kCounter);
    CHECK(first.first == kIterations * 2);
    CHECK(first.first == second.first);
    CHECK(first.second == second.second);
}

// Two cores continuously load through the MMU while a host thread flips the
// page-table entry and broadcasts TLB invalidations. The only acceptable
// loaded values are the two mapped pages; any mixed value would indicate an
// unsynchronized walk or TLB update.
void tlb_invalidation_stress() {
    winios::SMPCluster cluster(2, 0x20000);
    Memory& memory = cluster.get_memory();
    memory.set_concurrent_mode(true);

    const std::uint64_t l0_base = 0x2000;
    const std::uint64_t l1_base = 0x3000;
    const std::uint64_t l2_base = 0x4000;
    const std::uint64_t l3_base = 0x5000;
    const std::uint64_t page_a = 0x8000;
    const std::uint64_t page_b = 0x9000;

    memory.write_u64(l0_base, l1_base | 0x3ULL);
    memory.write_u64(l1_base, l2_base | 0x3ULL);
    memory.write_u64(l2_base, l3_base | 0x3ULL);
    memory.write_u64(l3_base + 0u, 0x0ULL | 0x3ULL | (1ULL << 10u));
    memory.write_u64(l3_base + 8u, page_a | 0x3ULL | (1ULL << 10u));
    memory.write_u64(page_a, 0xAAAAAAAAAAAAAAAAull);
    memory.write_u64(page_b, 0xBBBBBBBBBBBBBBBBull);

    memory.write_u32(0x100, kLdrX1X2);
    memory.write_u32(0x104, kBranchBack4);

    for (std::size_t i = 0; i < 2; ++i) {
        CPU& core = cluster.get_core(i);
        core.set_architectural_exceptions(true);
        core.set_ttbr0_el1(l0_base);
        core.set_sctlr_el1(0x00C50839ULL);
        core.write_register(2, 0x1000);
        core.set_program_counter(0x100);
    }

    std::atomic<bool> stop{false};
    std::thread invalidator([&] {
        for (std::size_t i = 0; i < 4000u; ++i) {
            memory.write_u64(l3_base + 8u,
                             ((i & 1u) ? page_b : page_a) | 0x3ULL | (1ULL << 10u));
            cluster.broadcast_tlbi_all(0);
            cluster.broadcast_tlbi_all(1);
        }
        stop.store(true, std::memory_order_release);
    });

    std::vector<std::thread> steppers;
    for (std::size_t i = 0; i < 2; ++i) {
        steppers.emplace_back([&, i] {
            CPU& core = cluster.get_core(i);
            while (!stop.load(std::memory_order_acquire)) {
                for (int k = 0; k < 200; ++k) {
                    core.step(memory);
                }
            }
        });
    }
    for (auto& thread : steppers) {
        thread.join();
    }
    invalidator.join();

    for (std::size_t i = 0; i < 2; ++i) {
        const std::uint64_t value = cluster.get_core(i).read_register(1);
        CHECK(value == 0xAAAAAAAAAAAAAAAAull
              || value == 0xBBBBBBBBBBBBBBBBull);
    }
    memory.set_concurrent_mode(false);
}

void start_stop_lifecycle() {
    winios::SMPCluster cluster(2, 0x10000);
    Memory& memory = cluster.get_memory();
    memory.write_u32(0x1000, kBranchSelf);
    cluster.initialize_core(0, 0x1000);
    cluster.halt_core(1);

    for (int i = 0; i < 5; ++i) {
        cluster.start_parallel();
        CHECK(cluster.parallel_started());
        CHECK(cluster.get_memory().concurrent_mode());
        cluster.run_parallel(100);
        cluster.stop_parallel();
        CHECK(!cluster.parallel_started());
        CHECK(!cluster.get_memory().concurrent_mode());
    }
}

} // namespace

int main() {
    memory_concurrent_disjoint_writes();
    memory_concurrent_torn_read_check();
    memory_store_observer_invalidates_reservation();
    gic_concurrent_mmio_and_evaluation();
    parallel_lse_atomic_counter();
    parallel_exclusive_counter();
    parallel_sev_wfe_wakeup();
    parallel_halted_core_does_not_execute();
    parallel_budget_stops_execution();
    parallel_exception_propagates();
    parallel_pause_resume();
    parallel_shared_clock_is_cluster_wide();
    deterministic_mode_is_repeatable();
    tlb_invalidation_stress();
    start_stop_lifecycle();
    return test::summarize("threading_tests");
}
