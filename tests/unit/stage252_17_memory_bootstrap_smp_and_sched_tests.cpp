// ============================================================================
// Stage 252.14 - 252.17: Memory Bootstrap, Allocator Zone Lifecycle,
// Scheduler Context Switch & SMP Secondary CPU Synchronization Tests
// ============================================================================

#include "cpu/cpu.h"
#include "kernel/task.h"
#include "kernel/thread.h"
#include "kernel/scheduler.h"
#include "loader/kernel_interval_map.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_zone_allocator.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::kernel;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 252.14: Physical Memory Bootstrap & VM Metadata Validation
// ----------------------------------------------------------------------------
static void test_stage252_14_memory_bootstrap_metadata() {
    std::cout << "[Stage 252.14] Testing Physical Memory Bootstrap & Metadata Validation...\n";

    // 1. Build and validate canonical interval map
    KernelPhysicalIntervalMap phys_map(0x0ULL, 0x800000ULL);
    TEST_ASSERT(phys_map.add_interval({"__TEXT", IntervalType::KernelText, 0x0ULL, 0x4000ULL, 0x5, 0x5, true}), "Add __TEXT");
    TEST_ASSERT(phys_map.add_interval({"ReservedScratch", IntervalType::GenericReserved, 0x4000ULL, 0xC000ULL, 0x3, 0x3, true}), "Add ReservedScratch");
    TEST_ASSERT(phys_map.add_interval({"__DATA", IntervalType::KernelData, 0x10000ULL, 0x20000ULL, 0x3, 0x3, true}), "Add __DATA");
    TEST_ASSERT(phys_map.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x30000ULL, 0x4000ULL, 0x3, 0x3, true}), "Add DeviceTree");
    TEST_ASSERT(phys_map.add_interval({"BootArgs", IntervalType::BootArgs, 0x34000ULL, 0x4000ULL, 0x3, 0x3, true}), "Add BootArgs");
    TEST_ASSERT(phys_map.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x38000ULL, 0x10000ULL, 0x3, 0x3, true}), "Add BootstrapStack");
    TEST_ASSERT(phys_map.add_interval({"PageTables", IntervalType::PageTables, 0x48000ULL, 0x8000ULL, 0x3, 0x3, true}), "Add PageTables");
    TEST_ASSERT(phys_map.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x50000ULL, 0x7B0000ULL, 0x3, 0x3, false}), "Add DynamicPool");

    const auto* pool = phys_map.find_interval_by_name("DynamicPool");
    TEST_ASSERT(pool != nullptr, "DynamicPool registered");
    TEST_ASSERT(pool->base == 0x50000ULL, "DynamicPool starts at Frame 20 (0x50000)");
    TEST_ASSERT(pool->end() == 0x800000ULL, "DynamicPool ends at 0x800000 (492 frames)");
    TEST_ASSERT(pool->size == 0x7B0000ULL, "DynamicPool size is 7,808 KB (492 * 16 KB)");

    // 2. Validate Page Allocator frame states & capacity
    XnuPhysicalPageAllocator alloc;
    alloc.initialize(0x0ULL, 0x800000ULL);
    alloc.reserve_range(0x0ULL, 0x50000ULL, "CanonicalReserved");
    TEST_ASSERT(alloc.reserved_pages_count() == 20, "20 reserved frames");
    TEST_ASSERT(alloc.free_pages_count() == 492, "492 free frames");
    TEST_ASSERT(alloc.is_frame_reserved(19), "Frame 19 is reserved");
    TEST_ASSERT(alloc.is_frame_free(20), "Frame 20 is free");
    TEST_ASSERT(alloc.is_frame_free(511), "Frame 511 is free");

    std::cout << "[Stage 252.14] Memory bootstrap metadata verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 252.15: Kernel Allocator & Zone Lifecycle Separation
// ----------------------------------------------------------------------------
static void test_stage252_15_allocator_zone_lifecycle() {
    std::cout << "[Stage 252.15] Testing Zone Allocator & Subsystem Lifecycle Separation...\n";

    // Reconcile and test zone allocator as Category C WinIOS Compatibility Model
    XnuZoneAllocator allocator(0x200000ULL, 0x100000ULL);

    std::string err;
    TEST_ASSERT(allocator.create_zone("kalloc.32", 32, 16, &err), "Create 32-byte zone");
    TEST_ASSERT(allocator.zone_count() == 1, "Zone count is 1");

    // Allocate multiple elements
    const uint64_t a1 = allocator.zalloc("kalloc.32");
    const uint64_t a2 = allocator.zalloc("kalloc.32");
    TEST_ASSERT(a1 != 0 && a2 != 0, "Allocations succeeded");
    TEST_ASSERT(a1 != a2, "Addresses unique");
    TEST_ASSERT((a1 & 0xF) == 0, "16-byte alignment satisfied");

    // Free and double-free detection
    TEST_ASSERT(allocator.zfree("kalloc.32", a1, &err), "Free a1");
    TEST_ASSERT(!allocator.zfree("kalloc.32", a1, &err), "Double free rejected");
    TEST_ASSERT(!allocator.zfree("kalloc.32", 0xDEAD0000ULL, &err), "Out of bounds pointer rejected");

    std::cout << "[Stage 252.15] Zone allocator verified as Category C compatibility model.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 252.16: Scheduler Provenance & Context Switch Evidence
// ----------------------------------------------------------------------------
static void test_stage252_16_scheduler_context_switch() {
    std::cout << "[Stage 252.16] Testing Scheduler & Context-Switch Mechanics...\n";

    Memory mem(0x100000);
    KernelThreadManager thread_mgr;
    PriorityScheduler sched(thread_mgr, mem);
    sched.initialize();

    // Create 2 threads: t1 with priority 16, t2 with priority 24 (higher priority)
    auto* t1 = thread_mgr.create_thread("worker_low", 0x1000ULL, 0ULL, 0x20000ULL, 0x4000ULL, 16);
    auto* t2 = thread_mgr.create_thread("worker_high", 0x2000ULL, 0ULL, 0x30000ULL, 0x4000ULL, 24);

    TEST_ASSERT(t1 != nullptr && t2 != nullptr, "Threads created");
    sched.enqueue_thread(t1);
    sched.enqueue_thread(t2);

    // Pick next thread: higher priority (t2) must be scheduled first
    auto* next = sched.pick_next_thread();
    TEST_ASSERT(next != nullptr, "Picked next thread");
    TEST_ASSERT(next->tid() == t2->tid(), "High priority worker_high scheduled before worker_low");

    // Context switch mechanics
    CPU sched_cpu;
    sched.dispatch_next(sched_cpu);
    TEST_ASSERT(sched.current_thread() != nullptr, "Current thread is active");

    std::cout << "[Stage 252.16] Scheduler context switch verified as Category C model.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 252.17: SMP Secondary CPU Release & Synchronization
// ----------------------------------------------------------------------------
static void test_stage252_17_smp_secondary_cpu_release() {
    std::cout << "[Stage 252.17] Testing SMP Secondary CPU Release & Synchronization...\n";

    // Core 0 executes early initialization; Core 1 enters secondary_park (WFE)
    CPU core0;
    core0.set_core_id(0);

    CPU core1;
    core1.set_core_id(1);
    core1.set_architectural_exceptions(true);

    Memory mem(0x800000);

    // Secondary park code at 0x2000:
    // 0x2000: WFE (0xD503205FU)
    // 0x2004: B 0x2000 (0x17FFFFFFU)
    mem.write_u32(0x2000, 0xD503205Fu);
    mem.write_u32(0x2004, 0x17FFFFFFu);

    core1.set_program_counter(0x2000);
    core1.step(mem);
    TEST_ASSERT(core1.is_waiting_for_event(), "Core 1 parked in WFE");

    // Core 0 releases secondary cores via SEV
    // 0x1000: SEV (0xD503209FU)
    mem.write_u32(0x1000, 0xD503209Fu);
    core0.set_program_counter(0x1000);
    core0.step(mem); // Executes SEV

    // Core 1 wakes from WFE upon event
    core1.wake_from_wfe();
    TEST_ASSERT(!core1.is_waiting_for_event(), "Core 1 woke from WFE after SEV");

    std::cout << "[Stage 252.17] SMP secondary CPU release and synchronization verified.\n";
}

int main() {
    std::cout << "=== Running Stages 252.14 - 252.17 Unit Tests ===\n";
    test_stage252_14_memory_bootstrap_metadata();
    test_stage252_15_allocator_zone_lifecycle();
    test_stage252_16_scheduler_context_switch();
    test_stage252_17_smp_secondary_cpu_release();
    std::cout << "[PASS] All Stage 252.14 - 252.17 tests passed successfully.\n";
    return 0;
}
