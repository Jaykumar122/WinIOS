#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_core_state.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_shared_memory.h"
#include "memory/memory.h"

#define STAGE68_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at " \
                      << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

namespace {

constexpr std::uint32_t kNop = 0xD503201Fu;

// -----------------------------------------------------------------------------
// Suite 1: Per-Core Architectural State Isolation
// -----------------------------------------------------------------------------
bool test_per_core_state_isolation() {
    std::cout << "[1] test_per_core_state_isolation...\n";
    SMPCluster cluster(4, 0x10000);

    for (std::size_t c = 0; c < 4; ++c) {
        auto& cpu = cluster.get_core(c);
        cpu.write_register(0, 0x1000 + c);
        cpu.write_register(1, 0x2000 + c);
        cpu.set_program_counter(0x4000 + c * 0x100);
        cpu.set_stack_pointer(0x8000 + c * 0x100);
        cpu.set_nzcv(static_cast<std::uint8_t>(c & 0xFu));
    }

    // Verify isolation: mutating core 0 does not alter other cores
    auto& cpu0 = cluster.get_core(0);
    cpu0.write_register(0, 0xDEADBEEF);
    cpu0.set_program_counter(0xFFFFFFFF);

    for (std::size_t c = 1; c < 4; ++c) {
        const auto& cpu = cluster.get_core(c);
        STAGE68_ASSERT(cpu.read_register(0) == (0x1000 + c), "Core X0 isolated");
        STAGE68_ASSERT(cpu.read_register(1) == (0x2000 + c), "Core X1 isolated");
        STAGE68_ASSERT(cpu.get_program_counter() == (0x4000 + c * 0x100), "Core PC isolated");
        STAGE68_ASSERT(cpu.get_stack_pointer() == (0x8000 + c * 0x100), "Core SP isolated");
        STAGE68_ASSERT(cpu.get_nzcv() == static_cast<std::uint8_t>(c & 0xFu), "Core NZCV isolated");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 2: Coherent Shared Memory & Core Tagging
// -----------------------------------------------------------------------------
bool test_shared_memory_core_tagging() {
    std::cout << "[2] test_shared_memory_core_tagging...\n";
    SmpSharedMemory shared_mem(0x10000, 4);

    // Core 0 writes data at 0x100
    shared_mem.write_u64(0, 0x100, 0xCAFEBABE12345678ULL);

    // Core 1, 2, 3 read the exact data written by Core 0
    STAGE68_ASSERT(shared_mem.read_u64(1, 0x100) == 0xCAFEBABE12345678ULL, "Core 1 reads Core 0 write");
    STAGE68_ASSERT(shared_mem.read_u64(2, 0x100) == 0xCAFEBABE12345678ULL, "Core 2 reads Core 0 write");
    STAGE68_ASSERT(shared_mem.read_u64(3, 0x100) == 0xCAFEBABE12345678ULL, "Core 3 reads Core 0 write");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 3: Cross-Core Reservation Invalidation on Stores
// -----------------------------------------------------------------------------
bool test_cross_core_reservation_invalidation() {
    std::cout << "[3] test_cross_core_reservation_invalidation...\n";
    SmpExclusiveMonitorManager monitor(4);
    SmpSharedMemory shared_mem(0x10000, 4);
    shared_mem.attach_exclusive_monitor(&monitor);

    // Core 0 loads exclusive at address 0x200
    monitor.load_exclusive(0, 0x200, 8);
    STAGE68_ASSERT(monitor.is_reservation_valid(0, 0x200, 8), "Core 0 reservation active");

    // Core 1 performs an ordinary store to 0x200
    shared_mem.write_u64(1, 0x200, 0x9999ULL);

    // Core 0's reservation must now be automatically invalidated by Core 1's store
    STAGE68_ASSERT(!monitor.is_reservation_valid(0, 0x200, 8),
                   "Core 0 reservation revoked by Core 1 store");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 4: Exclusive Monitor Manager Cross-Core Tracking
// -----------------------------------------------------------------------------
bool test_exclusive_monitor_manager() {
    std::cout << "[4] test_exclusive_monitor_manager...\n";
    SmpExclusiveMonitorManager monitor(4);

    // Core 0 reservation at 0x300, Core 1 reservation at 0x400
    monitor.load_exclusive(0, 0x300, 8);
    monitor.load_exclusive(1, 0x400, 8);

    STAGE68_ASSERT(monitor.is_reservation_valid(0, 0x300, 8), "Core 0 reservation valid");
    STAGE68_ASSERT(monitor.is_reservation_valid(1, 0x400, 8), "Core 1 reservation valid");

    // Invalidate 0x300 from writer Core 2
    monitor.invalidate_reservations(2, 0x300, 8);

    // Core 0 reservation revoked, Core 1 at 0x400 remains intact
    STAGE68_ASSERT(!monitor.is_reservation_valid(0, 0x300, 8), "Core 0 reservation invalidated");
    STAGE68_ASSERT(monitor.is_reservation_valid(1, 0x400, 8), "Core 1 reservation preserved");

    // Clear Core 1 reservation
    monitor.clear_reservation(1);
    STAGE68_ASSERT(!monitor.is_reservation_valid(1, 0x400, 8), "Core 1 reservation cleared");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 5: Cross-Core Atomic Exclusive Store Commits
// -----------------------------------------------------------------------------
bool test_cross_core_atomic_exclusive_store() {
    std::cout << "[5] test_cross_core_atomic_exclusive_store...\n";
    SmpExclusiveMonitorManager monitor(4);
    SmpSharedMemory shared_mem(0x10000, 4);
    shared_mem.attach_exclusive_monitor(&monitor);

    // Core 0 loads exclusive
    monitor.load_exclusive(0, 0x500, 8);

    // Core 0 commits exclusive write -> succeeds
    const bool success0 = shared_mem.write_exclusive_u64(0, 0x500, 0x1111222233334444ULL);
    STAGE68_ASSERT(success0 == true, "Core 0 exclusive store succeeds");
    STAGE68_ASSERT(shared_mem.read_u64(0, 0x500) == 0x1111222233334444ULL, "Memory committed");

    // Core 1 attempts exclusive write without reservation -> fails
    const bool success1 = shared_mem.write_exclusive_u64(1, 0x500, 0x9999ULL);
    STAGE68_ASSERT(success1 == false, "Core 1 exclusive store rejected without reservation");
    STAGE68_ASSERT(shared_mem.read_u64(1, 0x500) == 0x1111222233334444ULL, "Memory unmodified on failure");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 6: Cross-Core Spinlock Emulation
// -----------------------------------------------------------------------------
bool test_cross_core_spinlock_emulation() {
    std::cout << "[6] test_cross_core_spinlock_emulation...\n";
    SmpExclusiveMonitorManager monitor(2);
    SmpSharedMemory shared_mem(0x10000, 2);
    shared_mem.attach_exclusive_monitor(&monitor);

    constexpr std::uint64_t kLockAddress = 0x600;
    shared_mem.write_u64(0, kLockAddress, 0); // 0 = Unlocked

    // Helper for CAS try-lock: if 0 -> write 1
    auto try_lock = [&](std::size_t core) -> bool {
        monitor.load_exclusive(core, kLockAddress, 8);
        const std::uint64_t val = shared_mem.read_u64(core, kLockAddress);
        if (val != 0) {
            monitor.clear_reservation(core);
            return false;
        }
        return shared_mem.write_exclusive_u64(core, kLockAddress, 1);
    };

    auto unlock = [&](std::size_t core) {
        shared_mem.write_u64(core, kLockAddress, 0);
    };

    // Core 0 acquires lock
    STAGE68_ASSERT(try_lock(0) == true, "Core 0 acquires lock");
    STAGE68_ASSERT(shared_mem.read_u64(0, kLockAddress) == 1, "Lock state is 1");

    // Core 1 attempts to acquire -> fails
    STAGE68_ASSERT(try_lock(1) == false, "Core 1 fails to acquire locked lock");

    // Core 0 unlocks
    unlock(0);
    STAGE68_ASSERT(shared_mem.read_u64(0, kLockAddress) == 0, "Lock state restored to 0");

    // Core 1 now acquires successfully
    STAGE68_ASSERT(try_lock(1) == true, "Core 1 acquires lock after release");
    STAGE68_ASSERT(shared_mem.read_u64(1, kLockAddress) == 1, "Lock state is 1");

    unlock(1);
    return true;
}

// -----------------------------------------------------------------------------
// Suite 7: Inter-Processor Interrupt (SGI) Dispatch
// -----------------------------------------------------------------------------
bool test_sgi_dispatch() {
    std::cout << "[7] test_sgi_dispatch...\n";
    GICv3 gic;
    gic.set_core_count(2);

    GICv3::DistributorState dist = gic.get_distributor_state();
    dist.gicd_ctlr = 0x3u;
    gic.set_distributor_state(dist);

    for (std::size_t c = 0; c < 2; ++c) {
        auto& r = gic.get_redistributor(c);
        r.isenabler0 |= (1u << 1); // Enable SGI 1
        r.igroupr0   |= (1u << 1); // Group 1
        r.ipriorityr[1] = 0x60u;

        gic.write_icc_pmr(0xFF, c);
        gic.write_icc_igrpen1(1, c);
    }

    // Core 0 sends SGI 1 targeting Core 1
    // ICC_SGI1R_EL1: target_list in bits [15:0], INTID in bits [27:24]
    // Target Core 1 = bit 1 (0x2), INTID = 1 (1 << 24) -> 0x01000002
    gic.send_sgi(0, 0x01000002ULL);

    // Evaluate Core 1: SGI 1 must be pending
    auto sig1 = gic.evaluate(1, 0, 1);
    STAGE68_ASSERT(sig1.has_interrupt == true, "Core 1 has pending interrupt");
    STAGE68_ASSERT(sig1.intid == 1u, "Pending INTID on Core 1 is SGI 1");

    // Evaluate Core 0: No interrupt must be pending
    auto sig0 = gic.evaluate(1, 0, 0);
    STAGE68_ASSERT(sig0.has_interrupt == false, "Core 0 has no pending interrupt");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 8: Cross-Core TLB Invalidation Broadcast
// -----------------------------------------------------------------------------
bool test_tlb_broadcast() {
    std::cout << "[8] test_tlb_broadcast...\n";
    SMPCluster cluster(2, 0x10000);

    // Verify initial TLB stats
    const std::uint64_t init_inv1 = cluster.get_core(1).get_mmu().get_tlb_invalidations();

    // Core 0 broadcasts TLBI ALL across inner shareable domain
    cluster.broadcast_tlbi_all(0);

    // Core 1's TLB invalidation count must have increased
    const std::uint64_t post_inv1 = cluster.get_core(1).get_mmu().get_tlb_invalidations();
    STAGE68_ASSERT(post_inv1 > init_inv1, "Core 1 TLB invalidations increased on broadcast");

    return true;
}

// -----------------------------------------------------------------------------
// Suite 9: Deterministic SMP Round-Robin Scheduling
// -----------------------------------------------------------------------------
bool test_deterministic_scheduling() {
    std::cout << "[9] test_deterministic_scheduling...\n";
    SMPCluster cluster(3, 0x10000);

    // Place NOPs at address 0x0
    for (std::uint64_t a = 0; a < 0x40; a += 4) {
        cluster.get_memory().write_u32(a, kNop);
    }

    for (std::size_t c = 0; c < 3; ++c) {
        cluster.resume_core(c);
        cluster.get_core(c).set_program_counter(0);
    }

    // Step round robin: must execute 1 instruction on each of the 3 cores
    const std::size_t stepped = cluster.step_round_robin();
    STAGE68_ASSERT(stepped == 3, "Stepped all 3 cores");

    for (std::size_t c = 0; c < 3; ++c) {
        STAGE68_ASSERT(cluster.get_core(c).get_program_counter() == 4, "PC advanced by 4 on core");
    }

    return true;
}

// -----------------------------------------------------------------------------
// Suite 10: Canonical SMP State Hashing
// -----------------------------------------------------------------------------
bool test_smp_canonical_state_hash() {
    std::cout << "[10] test_smp_canonical_state_hash...\n";
    SMPCluster cluster1(2, 0x10000);
    SMPCluster cluster2(2, 0x10000);

    // Compute state hash across both clusters
    auto hash_core = [](const CPU& cpu) -> std::uint64_t {
        std::uint64_t h = cpu.get_program_counter() ^ (cpu.get_stack_pointer() << 5);
        for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
            h ^= cpu.read_register(i) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    };

    const std::uint64_t h1_0 = hash_core(cluster1.get_core(0));
    const std::uint64_t h2_0 = hash_core(cluster2.get_core(0));
    STAGE68_ASSERT(h1_0 == h2_0, "Core 0 hashes match for fresh clusters");

    // Mutate Core 0 on cluster 1
    cluster1.get_core(0).write_register(1, 0x1234);
    const std::uint64_t h1_0_mut = hash_core(cluster1.get_core(0));
    STAGE68_ASSERT(h1_0 != h1_0_mut, "Hash changes on register mutation");

    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 68 SMP Runtime Tests ===" << std::endl;

    if (!test_per_core_state_isolation()) return 1;
    if (!test_shared_memory_core_tagging()) return 2;
    if (!test_cross_core_reservation_invalidation()) return 3;
    if (!test_exclusive_monitor_manager()) return 4;
    if (!test_cross_core_atomic_exclusive_store()) return 5;
    if (!test_cross_core_spinlock_emulation()) return 6;
    if (!test_sgi_dispatch()) return 7;
    if (!test_tlb_broadcast()) return 8;
    if (!test_deterministic_scheduling()) return 9;
    if (!test_smp_canonical_state_hash()) return 10;

    std::cout << "stage68_smp_runtime_tests: all 10 test suites passed successfully" << std::endl;
    return 0;
}
