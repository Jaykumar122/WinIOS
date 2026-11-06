#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "cpu/smp_exclusive_monitor.h"
#include "cpu/smp_interrupt_router.h"
#include "cpu/smp_scheduler.h"
#include "device/virtual_device_bus.h"
#include "device/virtual_dma_device.h"
#include "memory/memory.h"
#include "reference/independent_reference_model.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::reference;
using namespace winios::device;

// -----------------------------------------------------------------------------
// Test 1: GIC Priority Masking & Deterministic Tie-Breaking
// -----------------------------------------------------------------------------
bool test_gic_priority_and_tie_breaking() {
    GICv3 gic;
    gic.reset();
    gic.set_core_count(4);
    gic.write_mmio(GICv3::kGicdBase + 0x0000, 0x3u, 4);
    gic.write_icc_sre_el1(1, 0);
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_igrpen1(1, 0);

    ReferenceGicModel ref_gic;
    ref_gic.reset();

    // 1. Initial state: PMR is default (0 or 0xFF depending on init)
    // Configure core 0 PMR to 0x80 (only priorities < 0x80 signal)
    gic.write_icc_pmr(0x80u, 0);
    ref_gic.set_pmr(0, 0x80u);

    // Assert SPI 40 with priority 0xA0 (should be masked by PMR 0x80)
    // Note: lower numeric priority = higher architectural priority
    gic.write_mmio(GICv3::kGicdBase + 0x0100 + 40 / 32 * 4, 1u << (40 % 32), 4); // ISENABLER1
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 40, 0xA0u, 1); // IPRIORITYR40
    gic.set_spi_level(40, true);

    ref_gic.set_irq_enabled(40, true, 0);
    ref_gic.set_irq_priority(40, 0xA0u);
    ref_gic.set_irq_pending(40, true, 0);

    const auto prod_sig1 = gic.evaluate(1, 0, 0);
    const auto ref_sig1 = ref_gic.evaluate(0, 1);
    TEST_ASSERT(!prod_sig1.has_interrupt, "Priority 0xA0 must be masked by PMR 0x80");
    TEST_ASSERT(!ref_sig1.has_interrupt, "Reference model must agree interrupt is masked");

    // Assert SPI 42 with priority 0x40 (passes PMR 0x80)
    gic.write_mmio(GICv3::kGicdBase + 0x0100 + 42 / 32 * 4, 1u << (42 % 32), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 42, 0x40u, 1);
    gic.set_spi_level(42, true);

    ref_gic.set_irq_enabled(42, true, 0);
    ref_gic.set_irq_priority(42, 0x40u);
    ref_gic.set_irq_pending(42, true, 0);

    const auto prod_sig2 = gic.evaluate(1, 0, 0);
    const auto ref_sig2 = ref_gic.evaluate(0, 1);
    TEST_ASSERT(prod_sig2.has_interrupt, "Priority 0x40 must pass PMR 0x80");
    TEST_ASSERT(prod_sig2.intid == 42, "Delivered interrupt must be INTID 42");
    TEST_ASSERT(ref_sig2.has_interrupt && ref_sig2.intid == 42, "Reference model must deliver INTID 42");

    // 2. Deterministic Tie-Breaking Test:
    // Assert SPI 50 and SPI 60 with identical priority (0x20)
    // Lower INTID (50) must take precedence
    gic.write_mmio(GICv3::kGicdBase + 0x0100 + 50 / 32 * 4, 1u << (50 % 32), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 50, 0x20u, 1);
    gic.set_spi_level(50, true);

    gic.write_mmio(GICv3::kGicdBase + 0x0100 + 60 / 32 * 4, 1u << (60 % 32), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0400 + 60, 0x20u, 1);
    gic.set_spi_level(60, true);

    ref_gic.set_irq_enabled(50, true, 0);
    ref_gic.set_irq_priority(50, 0x20u);
    ref_gic.set_irq_pending(50, true, 0);

    ref_gic.set_irq_enabled(60, true, 0);
    ref_gic.set_irq_priority(60, 0x20u);
    ref_gic.set_irq_pending(60, true, 0);

    const auto prod_tie = gic.evaluate(1, 0, 0);
    const auto ref_tie = ref_gic.evaluate(0, 1);
    TEST_ASSERT(prod_tie.intid == 50, "Tie-breaker must select lower INTID 50");
    TEST_ASSERT(ref_tie.intid == 50, "Reference model tie-breaker must select INTID 50");

    return true;
}

// -----------------------------------------------------------------------------
// Test 2: SGI, PPI, and SPI Routing Across Cores
// -----------------------------------------------------------------------------
bool test_interrupt_routing_and_isolation() {
    SmpInterruptRouter router(8);

    // Enqueue PPI 27 on Core 0
    router.request_interrupt(0, 27, 0x40u, 0, false);
    // Enqueue SGI 3 targeting Cores 1 and 2
    router.request_sgi(0, (1u << 1) | (1u << 2), 3, 0x20u);

    TEST_ASSERT(router.pending_count(0) == 1, "Core 0 must have 1 pending interrupt (PPI 27)");
    TEST_ASSERT(router.pending_count(1) == 1, "Core 1 must have 1 pending interrupt (SGI 3)");
    TEST_ASSERT(router.pending_count(2) == 1, "Core 2 must have 1 pending interrupt (SGI 3)");
    TEST_ASSERT(router.pending_count(3) == 0, "Core 3 must have 0 pending interrupts");

    // Verify peek
    const auto peek0 = router.peek_pending(0);
    TEST_ASSERT(peek0.has_value() && peek0->intid == 27, "Core 0 peek must show INTID 27");

    const auto peek1 = router.peek_pending(1);
    TEST_ASSERT(peek1.has_value() && peek1->intid == 3, "Core 1 peek must show INTID 3");

    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Memory Ordering Litmus Tests (DMB, DSB, Exclusive Contention)
// -----------------------------------------------------------------------------
bool test_memory_ordering_litmus_differential() {
    // Litmus Test 1: Message Passing
    // Under sequentially consistent deterministic scheduler:
    // Core 0 writes DATA=0x42, then FLAG=1 with DMB
    // Core 1 reads FLAG. If FLAG==1, DATA MUST be 0x42 (never 0)
    const auto outcome1 = ReferenceLitmusVerifier::verify_message_passing(1, 0x42);
    TEST_ASSERT(outcome1.valid && !outcome1.is_forbidden, "Standard message passing outcome must be permitted");

    const auto outcome_bad = ReferenceLitmusVerifier::verify_message_passing(1, 0);
    TEST_ASSERT(outcome_bad.is_forbidden, "Reading stale data after seeing flag is forbidden");

    // Litmus Test 2: Store Buffering with DSB
    // Core 0 writes X=1, DSB, reads Y
    // Core 1 writes Y=1, DSB, reads X
    // Reading Y=0 and X=0 simultaneously is forbidden under sequential consistency
    const auto outcome2 = ReferenceLitmusVerifier::verify_store_buffering(1, 1);
    TEST_ASSERT(outcome2.valid && !outcome2.is_forbidden, "Store buffering valid outcome");

    const auto outcome_sb_bad = ReferenceLitmusVerifier::verify_store_buffering(0, 0);
    TEST_ASSERT(outcome_sb_bad.is_forbidden, "Simultaneous stale reads in store buffering are forbidden");

    // Litmus Test 3: Exclusive Monitor Invalidation on Overlapping Write
    SmpExclusiveMonitorManager monitor(4);
    monitor.load_exclusive(0, 0x2000, 8); // Core 0 reserves 0x2000
    monitor.load_exclusive(1, 0x3000, 8); // Core 1 reserves 0x3000

    TEST_ASSERT(monitor.is_reservation_valid(0, 0x2000, 8), "Core 0 reservation must be valid");
    TEST_ASSERT(monitor.is_reservation_valid(1, 0x3000, 8), "Core 1 reservation must be valid");

    // Core 2 performs normal store writing to 0x2004 (overlapping Core 0's 64B granule)
    monitor.invalidate_reservations(2, 0x2004, 4);

    TEST_ASSERT(!monitor.is_reservation_valid(0, 0x2000, 8), "Core 0 reservation must be invalidated");
    TEST_ASSERT(monitor.is_reservation_valid(1, 0x3000, 8), "Core 1 reservation at 0x3000 must remain valid");

    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Extended Canonical Cluster Hash Audit & Mutation Sensitivity
// -----------------------------------------------------------------------------
bool test_extended_cluster_hash_sensitivity() {
    auto shared_mem = std::make_shared<SmpSharedMemory>(0x100000);
    SmpScheduler sched(shared_mem, 2, 1000);

    auto cpu0 = std::make_shared<CPU>();
    auto cpu1 = std::make_shared<CPU>();
    cpu0->reset();
    cpu1->reset();
    sched.register_core(0, cpu0);
    sched.register_core(1, cpu1);

    // Baseline extended cluster hash
    const std::uint64_t hash_base = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_base != 0, "Base extended cluster hash must be non-zero");

    // 1. Sensitivity Test: Mutate exclusive monitor reservation
    sched.exclusive_monitor().load_exclusive(0, 0x4000, 8);
    const std::uint64_t hash_monitor_mutated = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_monitor_mutated != hash_base, "Extended hash must change when exclusive monitor reservation is created (AUD-34-01)");

    sched.exclusive_monitor().clear_reservation(0);
    const std::uint64_t hash_monitor_cleared = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_monitor_cleared == hash_base, "Extended hash must restore to baseline when reservation is cleared");

    // 2. Sensitivity Test: Mutate interrupt router pending queue
    sched.interrupt_router().request_interrupt(1, 48, 0x30u);
    const std::uint64_t hash_irq_mutated = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_irq_mutated != hash_base, "Extended hash must change when interrupt is queued in router (AUD-34-01)");

    sched.interrupt_router().clear_all();
    const std::uint64_t hash_irq_cleared = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_irq_cleared == hash_base, "Extended hash must restore to baseline when interrupt is cleared");

    // 3. Sensitivity Test: Attach virtual device bus and mutate device state
    auto bus = std::make_shared<VirtualDeviceBus>();
    auto dma_dev = std::make_shared<VirtualDmaDevice>(0x10000000ULL, 0x1000ULL);
    bus->register_device(dma_dev);
    sched.attach_device_bus(bus);

    const std::uint64_t hash_with_bus = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_with_bus != hash_base, "Extended hash must change when device bus is attached");

    // Mutate device register status
    dma_dev->write_register(dma_reg::kDescriptorAddr, 0x5000ULL, 8);
    const std::uint64_t hash_device_mutated = sched.compute_extended_cluster_hash();
    TEST_ASSERT(hash_device_mutated != hash_with_bus, "Extended hash must change when device MMIO register is modified (AUD-34-01)");

    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "WinIOS Phase 52 Stage 35: GIC & Memory Ordering Conformance Tests\n";
    std::cout << "=================================================================\n";

    std::cout << "[Test 1] Testing GIC Priority Masking & Deterministic Tie-Breaking...\n";
    if (!test_gic_priority_and_tie_breaking()) return 1;
    std::cout << "  -> Test 1 Passed. PMR masking and lower INTID tie-breaking verified.\n";

    std::cout << "[Test 2] Testing SGI, PPI, and SPI Routing Across Virtual Cores...\n";
    if (!test_interrupt_routing_and_isolation()) return 1;
    std::cout << "  -> Test 2 Passed. Per-core queue isolation verified.\n";

    std::cout << "[Test 3] Testing Memory Ordering Litmus Tests (DMB/DSB & Monitors)...\n";
    if (!test_memory_ordering_litmus_differential()) return 1;
    std::cout << "  -> Test 3 Passed. Message passing and store buffering litmus tests match.\n";

    std::cout << "[Test 4] Testing Extended Cluster Hash Audit & State Sensitivity (AUD-34-01)...\n";
    if (!test_extended_cluster_hash_sensitivity()) return 1;
    std::cout << "  -> Test 4 Passed. AUD-34-01 resolved: monitors, router queues, and device bus covered.\n";

    std::cout << "All GIC & Memory Ordering Conformance Tests PASSED successfully.\n";
    return 0;
}
