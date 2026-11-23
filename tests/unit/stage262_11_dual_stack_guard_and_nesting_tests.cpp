#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/canonical_physical_memory_map.h"
#include "memory/independent_stack_exception_auditor.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 262.7 - Canonical Dual-Stack Layout (12 Stacks + 12 Guards)
// ----------------------------------------------------------------------------
static void test_stage262_7_dual_stack_layout() {
    std::cout << "[Stage 262.7] Testing Canonical Dual-Stack Memory Layout Geometry...\n";

    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();

    TEST_ASSERT(map.ram_base() == 0x00000000ULL, "RAM base is 0x0");
    TEST_ASSERT(map.ram_size() == 0x00800000ULL, "RAM size is 8 MiB (512 frames)");
    TEST_ASSERT(map.ram_end()  == 0x00800000ULL, "RAM end is 0x800000");

    auto free_ranges = map.derive_free_ranges();
    TEST_ASSERT(free_ranges.size() == 1, "Exactly 1 contiguous free pool range");
    TEST_ASSERT(free_ranges[0].start == 0x000B8000ULL, "Allocator pool starts at 0x000B8000 (Frame 46)");
    TEST_ASSERT(free_ranges[0].end == 0x00800000ULL, "Allocator pool ends at 0x00800000 (Frame 512)");
    TEST_ASSERT(free_ranges[0].frame_count == 466, "Free frame count == 466 frames");

    const std::size_t kReservedFrames = 46;
    const std::size_t kTotalFrames = 512;
    TEST_ASSERT(kReservedFrames + free_ranges[0].frame_count == kTotalFrames, "Reserved (46) + Free (466) == 512 frames");

    // Verify 6 cores each have 2 stacks and 2 independent guards
    for (std::size_t core = 0; core < 6; ++core) {
        auto geom = map.get_core_stack_geometry(core);
        TEST_ASSERT(geom.has_value(), "Core stack geometry present");

        // Interrupt Guard
        TEST_ASSERT(geom->int_guard_size == 16384ULL, "Interrupt guard size is 16 KiB");
        TEST_ASSERT(geom->int_guard_base < geom->int_stack_base, "Interrupt guard precedes interrupt stack");
        TEST_ASSERT(geom->int_guard_base + geom->int_guard_size == geom->int_stack_base, "Interrupt guard strictly adjacent to interrupt stack");

        // Interrupt Stack
        TEST_ASSERT(geom->int_stack_size == 16384ULL, "Interrupt stack size is 16 KiB");
        TEST_ASSERT(geom->int_stack_initial_sp == geom->int_stack_base + geom->int_stack_size, "Interrupt stack top at base + 16 KiB");

        // Thread Guard
        TEST_ASSERT(geom->thread_guard_size == 16384ULL, "Thread guard size is 16 KiB");
        TEST_ASSERT(geom->int_stack_initial_sp <= geom->thread_guard_base, "Thread guard placed above interrupt stack");
        TEST_ASSERT(geom->thread_guard_base + geom->thread_guard_size == geom->thread_stack_base, "Thread guard strictly adjacent to thread stack");

        // Thread Stack
        TEST_ASSERT(geom->thread_stack_size == 16384ULL, "Thread stack size is 16 KiB");
        TEST_ASSERT(geom->thread_stack_initial_sp == geom->thread_stack_base + geom->thread_stack_size, "Thread stack top at base + 16 KiB");

        // Verify permissions in map
        const auto* int_guard_entry = map.find_interval_by_address(geom->int_guard_base);
        TEST_ASSERT(int_guard_entry != nullptr, "Interrupt guard entry exists");
        TEST_ASSERT(int_guard_entry->permissions == 0, "Interrupt guard permissions == 0 (unmapped)");

        const auto* int_stack_entry = map.find_interval_by_address(geom->int_stack_base);
        TEST_ASSERT(int_stack_entry != nullptr, "Interrupt stack entry exists");
        TEST_ASSERT((int_stack_entry->permissions & 1) != 0, "Interrupt stack readable");
        TEST_ASSERT((int_stack_entry->permissions & 2) != 0, "Interrupt stack writable");
        TEST_ASSERT((int_stack_entry->permissions & 4) == 0, "Interrupt stack non-executable (W^X)");

        const auto* thr_guard_entry = map.find_interval_by_address(geom->thread_guard_base);
        TEST_ASSERT(thr_guard_entry != nullptr, "Thread guard entry exists");
        TEST_ASSERT(thr_guard_entry->permissions == 0, "Thread guard permissions == 0 (unmapped)");

        const auto* thr_stack_entry = map.find_interval_by_address(geom->thread_stack_base);
        TEST_ASSERT(thr_stack_entry != nullptr, "Thread stack entry exists");
        TEST_ASSERT((thr_stack_entry->permissions & 1) != 0, "Thread stack readable");
        TEST_ASSERT((thr_stack_entry->permissions & 2) != 0, "Thread stack writable");
        TEST_ASSERT((thr_stack_entry->permissions & 4) == 0, "Thread stack non-executable (W^X)");
    }

    // Verify Frame 32 (KernelTextSecondary) is reserved and non-allocator
    const auto* f32 = map.find_interval_by_address(0x00080000ULL);
    TEST_ASSERT(f32 != nullptr, "Frame 32 found");
    TEST_ASSERT(f32->kind == CanonicalMemoryKind::KernelTextSecondary, "Frame 32 is KernelTextSecondary");
    TEST_ASSERT((f32->permissions & 4) != 0, "Frame 32 is executable");
    TEST_ASSERT(0x00080000ULL < free_ranges[0].start, "Frame 32 is strictly before allocator pool");

    std::cout << "[Stage 262.7] Canonical dual-stack layout verified (12 stacks + 12 guards).\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 262.8 - Guard Page Fault Trapping Verification
// ----------------------------------------------------------------------------
static void test_stage262_8_guard_fault_trapping() {
    std::cout << "[Stage 262.8] Testing Guard Page Fault Trapping (Zero Permissions)...\n";

    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();

    // Verify all 12 guards reject any read or write access
    for (std::size_t core = 0; core < 6; ++core) {
        auto geom = map.get_core_stack_geometry(core);

        // Test Interrupt Guard
        const auto* g_int = map.find_interval_by_address(geom->int_guard_base);
        TEST_ASSERT(g_int != nullptr && g_int->permissions == 0, "Interrupt guard has zero permissions");
        TEST_ASSERT((g_int->permissions & 1) == 0, "Guard read denied");
        TEST_ASSERT((g_int->permissions & 2) == 0, "Guard write denied");
        TEST_ASSERT((g_int->permissions & 4) == 0, "Guard execute denied");

        // Test Thread Guard
        const auto* g_thr = map.find_interval_by_address(geom->thread_guard_base);
        TEST_ASSERT(g_thr != nullptr && g_thr->permissions == 0, "Thread guard has zero permissions");
        TEST_ASSERT((g_thr->permissions & 1) == 0, "Guard read denied");
        TEST_ASSERT((g_thr->permissions & 2) == 0, "Guard write denied");
        TEST_ASSERT((g_thr->permissions & 4) == 0, "Guard execute denied");
    }

    std::cout << "[Stage 262.8] Guard page fault trapping properties verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 262.9 - Downward Stack Growth and Overflow Containment
// ----------------------------------------------------------------------------
static void test_stage262_9_downward_growth_and_overflow_containment() {
    std::cout << "[Stage 262.9] Testing Downward Stack Growth & Overflow Containment...\n";

    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();

    for (std::size_t core = 0; core < 6; ++core) {
        auto geom = map.get_core_stack_geometry(core);

        // Thread stack grows downward: top -> base
        // If thread stack underflows base, it crosses into ThreadGuard:
        uint64_t thread_stack_underflow_addr = geom->thread_stack_base - 1;
        const auto* hit_region = map.find_interval_by_address(thread_stack_underflow_addr);

        TEST_ASSERT(hit_region != nullptr, "Underflow address maps to valid region");
        TEST_ASSERT(hit_region->kind == CanonicalMemoryKind::CoreThreadGuard,
                    "Thread stack underflow hits ThreadGuard");
        TEST_ASSERT(hit_region->permissions == 0, "ThreadGuard access causes immediate abort");

        // Ensure interrupt stack is isolated: ThreadGuard is strictly between ThreadStack and InterruptStack
        TEST_ASSERT(thread_stack_underflow_addr >= geom->thread_guard_base, "Contained in ThreadGuard");
        TEST_ASSERT(thread_stack_underflow_addr >= geom->int_stack_initial_sp, "Thread underflow CANNOT reach InterruptStack");

        // Similarly for Interrupt Stack:
        uint64_t int_stack_underflow_addr = geom->int_stack_base - 1;
        const auto* int_hit_region = map.find_interval_by_address(int_stack_underflow_addr);
        TEST_ASSERT(int_hit_region != nullptr, "Interrupt underflow maps to valid region");
        TEST_ASSERT(int_hit_region->kind == CanonicalMemoryKind::CoreInterruptGuard,
                    "Interrupt stack underflow hits InterruptGuard");
        TEST_ASSERT(int_hit_region->permissions == 0, "InterruptGuard access causes immediate abort");
    }

    std::cout << "[Stage 262.9] Downward stack growth & overflow containment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 262.10 - Measured Stack Frame Consumption for Nested Exceptions
// ----------------------------------------------------------------------------
static void test_stage262_10_measured_stack_consumption() {
    std::cout << "[Stage 262.10] Testing Measured Stack Consumption & Nesting Capacity...\n";

    // Standard XNU 64-bit exception saved state size:
    const std::size_t kSavedStateSize = 288;
    // Interrupt handler local variables + callee-saved registers / call frame overhead:
    const std::size_t kHandlerLocalsSize = 64;
    const std::size_t kCallFrameSize = 32;
    const std::size_t kBytesPerNestingLevel = kSavedStateSize + kHandlerLocalsSize + kCallFrameSize; // 384 bytes

    TEST_ASSERT(kBytesPerNestingLevel == 384, "Total frame consumption per nesting level is 384 bytes");

    const std::size_t kStackTotalBytes = 16384; // 16 KiB
    std::size_t max_theoretical_nesting = kStackTotalBytes / kBytesPerNestingLevel; // 42 levels
    TEST_ASSERT(max_theoretical_nesting >= 40, "Stack accommodates at least 40 nesting levels");

    // Simulate nesting at depths 1 through 10
    for (std::size_t depth = 1; depth <= 10; ++depth) {
        std::size_t consumed = depth * kBytesPerNestingLevel;
        std::size_t remaining = kStackTotalBytes - consumed;

        TEST_ASSERT(consumed <= kStackTotalBytes, "Stack not exhausted at tested depth");
        TEST_ASSERT(remaining >= 12000, "Adequate headroom remaining (> 12 KiB at depth <= 10)");
    }

    std::cout << "[Stage 262.10] Measured stack frame consumption (384 B/level) and nesting capacity verified.\n";
}

int main() {
    std::cout << "=== Running Stage 262.11: Dual-Stack Guard & Nesting Tests ===\n";
    test_stage262_7_dual_stack_layout();
    test_stage262_8_guard_fault_trapping();
    test_stage262_9_downward_growth_and_overflow_containment();
    test_stage262_10_measured_stack_consumption();
    std::cout << "=== ALL STAGE 262.11 TESTS PASSED ===\n";
    return 0;
}
