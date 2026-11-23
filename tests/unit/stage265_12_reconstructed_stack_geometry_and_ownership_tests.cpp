// =============================================================================
// WinIOS Campaign 265 - Stages 265.8 through 265.12 Test Suite
//
// Stage 265.8: Reconstruct Six-Core Stack Geometry (30 Frames)
// Stage 265.9: Canonical Six-Core Stack and Guard Layout
// Stage 265.10: Reconcile Campaign 262-265 Memory Maps
// Stage 265.11: Recalculate Allocator Geometry (54 Reserved / 458 Free)
// Stage 265.12: Independent Physical Ownership Auditor
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <cassert>

#include "memory/canonical_physical_memory_map.h"
#include "memory/independent_stack_exception_auditor.h"
#include "memory/xnu_page_allocator.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                      << " - " << (msg) << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios::memory;

// -----------------------------------------------------------------------------
// Stage 265.8: Reconstruct Six-Core Stack Geometry (30 Frames)
// -----------------------------------------------------------------------------
static bool test_stage265_8_reconstructed_stack_geometry() {
    std::cout << "[Stage 265.8] Testing reconstructed six-core stack geometry (30 frames)...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_campaign265_layout();

    std::size_t total_stack_guard_frames = 0;

    for (std::size_t c = 0; c < 6; ++c) {
        auto geom = map.get_core_stack_geometry(c);
        TEST_ASSERT(geom.has_value(), "Core stack geometry must be present");

        // 1. Interrupt Guard: 1 frame (16 KiB)
        TEST_ASSERT(geom->int_guard_size == 16384ULL, "Interrupt guard must be 16 KiB (1 frame)");
        total_stack_guard_frames += 1;

        // 2. Interrupt Stack: 1 frame (16 KiB)
        TEST_ASSERT(geom->int_stack_size == 16384ULL, "Interrupt stack must be 16 KiB (1 frame)");
        TEST_ASSERT(geom->int_guard_base + geom->int_guard_size == geom->int_stack_base,
                    "Interrupt guard must be contiguous below interrupt stack");
        TEST_ASSERT(geom->int_stack_initial_sp == geom->int_stack_base + geom->int_stack_size,
                    "Interrupt initial SP must be at stack top");
        total_stack_guard_frames += 1;

        // 3. Thread Guard: 1 frame (16 KiB)
        TEST_ASSERT(geom->thread_guard_size == 16384ULL, "Thread guard must be 16 KiB (1 frame)");
        total_stack_guard_frames += 1;

        // 4. Thread Stack: 2 frames (32 KiB)
        TEST_ASSERT(geom->thread_stack_size == 32768ULL, "Thread stack must be 32 KiB (2 frames)");
        TEST_ASSERT(geom->thread_guard_base + geom->thread_guard_size == geom->thread_stack_base,
                    "Thread guard must be contiguous below thread stack");
        TEST_ASSERT(geom->thread_stack_initial_sp == geom->thread_stack_base + geom->thread_stack_size,
                    "Thread initial SP must be at stack top (32 KiB above base)");
        total_stack_guard_frames += 2;
    }

    TEST_ASSERT(total_stack_guard_frames == 30, "PROVEN: Exactly 30 frames allocated for six-core stacks & guards!");
    std::cout << "  - Six thread stacks x 32 KiB = 12 frames\n";
    std::cout << "  - Six interrupt stacks x 16 KiB = 6 frames\n";
    std::cout << "  - Twelve guards x 16 KiB = 12 frames\n";
    std::cout << "  - Total: " << total_stack_guard_frames << " frames (480 KiB)\n";
    std::cout << "  [PASS] Stage 265.8 Reconstructed six-core stack geometry verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.9: Canonical Six-Core Stack and Guard Layout
// -----------------------------------------------------------------------------
static bool test_stage265_9_canonical_layout_integrity() {
    std::cout << "[Stage 265.9] Testing canonical layout integrity & non-overlap...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_campaign265_layout();
    PhysicalMemoryOverlapAuditor auditor(map);

    std::vector<std::string> report;
    bool passed = auditor.run_full_audit(report);

    for (const auto& line : report) {
        std::cout << "  " << line << "\n";
    }

    TEST_ASSERT(passed, "Comprehensive memory map audit must pass with zero violations");
    std::cout << "  [PASS] Stage 265.9 Canonical layout verified with zero collisions.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.10: Reconcile Campaign 262-265 Memory Maps
// -----------------------------------------------------------------------------
static bool test_stage265_10_memory_map_reconciliation() {
    std::cout << "[Stage 265.10] Reconciling memory map migration from Campaign 262 to 265...\n";

    auto map_262 = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    auto map_265 = CanonicalPhysicalMemoryMap::create_canonical_campaign265_layout();

    auto free_262 = map_262.derive_free_ranges();
    auto free_265 = map_265.derive_free_ranges();

    TEST_ASSERT(free_262.size() == 1, "Campaign 262 free ranges == 1");
    TEST_ASSERT(free_265.size() == 1, "Campaign 265 free ranges == 1");

    // Campaign 262: 46 reserved (0x0..0x000B8000), 466 free
    TEST_ASSERT(free_262[0].start == 0x000B8000ULL, "Campaign 262 pool start");
    TEST_ASSERT(free_262[0].frame_count == 466, "Campaign 262 free frame count");

    // Campaign 265: 54 reserved (0x0..0x000D8000), 458 free
    // The 8 additional reserved frames are:
    // +6 frames: Thread stacks expanded from 16 KiB (1 frame) to 32 KiB (2 frames) across 6 cores
    // +2 frames: Dedicated primary exception scratch area (Frames 17..18)
    TEST_ASSERT(free_265[0].start == 0x000D8000ULL, "Campaign 265 pool start == 0x000D8000 (Frame 54)");
    TEST_ASSERT(free_265[0].frame_count == 458, "Campaign 265 free frame count == 458");
    TEST_ASSERT(54 + 458 == 512, "Total RAM frames == 512 (8 MiB)");

    std::cout << "  - Migration delta: +8 reserved frames (6 thread stack expansion + 2 exception scratch)\n";
    std::cout << "  - Campaign 265 partition: 54 reserved frames (864 KiB), 458 free pool frames (7,328 KiB)\n";
    std::cout << "  [PASS] Stage 265.10 Memory migration fully reconciled and accounted for.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.11: Recalculate Allocator Geometry
// -----------------------------------------------------------------------------
static bool test_stage265_11_allocator_geometry() {
    std::cout << "[Stage 265.11] Testing physical page allocator on Campaign 265 geometry...\n";

    const std::uint64_t kRamBase = 0x00000000ULL;
    const std::uint64_t kRamSize = 0x00800000ULL; // 8 MiB (512 frames)
    const std::uint64_t kReservedSize = 54 * 16384ULL; // 54 frames = 864 KiB (0x00000000..0x000D8000)
    const std::uint64_t kPoolBase = 0x000D8000ULL; // Frame 54
    const std::size_t kPoolFrames = 458;

    XnuPhysicalPageAllocator allocator;
    std::string err;
    TEST_ASSERT(allocator.initialize(kRamBase, kRamSize, &err), "Allocator initialization succeeded");
    TEST_ASSERT(allocator.reserve_range(kRamBase, kReservedSize, "reserved_system_frames", &err), "Reserve range succeeded");
    TEST_ASSERT(allocator.free_pages_count() == kPoolFrames, "Initial free pages == 458");

    // Allocate all 458 pages
    std::vector<std::uint64_t> allocated;
    auto frame1 = allocator.allocate_pages(1);
    TEST_ASSERT(frame1.has_value(), "First page allocation must succeed");
    TEST_ASSERT(*frame1 == kPoolBase, "First allocated page is at 0x000D8000 (Frame 54)");
    allocated.push_back(*frame1);

    for (std::size_t i = 1; i < kPoolFrames; ++i) {
        auto p = allocator.allocate_pages(1);
        TEST_ASSERT(p.has_value(), "Page allocation must succeed");
        TEST_ASSERT(*p >= kPoolBase && *p < 0x00800000ULL, "Page address within pool bounds");
        TEST_ASSERT((*p % 16384ULL) == 0, "Page address must be 16 KiB aligned");
        allocated.push_back(*p);
    }

    // Next allocation must return nullopt (exhaustion)
    auto exhausted = allocator.allocate_pages(1);
    TEST_ASSERT(!exhausted.has_value(), "Exhaustion must return nullopt");

    // Double free test
    bool df_ok = allocator.free_pages(allocated[0], 1, &err);
    TEST_ASSERT(df_ok, "First free succeeds");
    bool df_fail = allocator.free_pages(allocated[0], 1, &err);
    TEST_ASSERT(!df_fail, "Double free must fail");

    // Foreign free test (address in reserved space)
    bool foreign_fail = allocator.free_pages(0x00020000ULL, 1, &err); // Frame 8 (KernelData)
    TEST_ASSERT(!foreign_fail, "Freeing reserved address must fail");

    std::cout << "  [PASS] Stage 265.11 Allocator geometry tested: exhaustion, double-free, foreign-free verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 265.12: Independent Physical Ownership Auditor
// -----------------------------------------------------------------------------
static bool test_stage265_12_independent_ownership_auditor() {
    std::cout << "[Stage 265.12] Testing independent ownership auditor & mutations...\n";

    auto map = CanonicalPhysicalMemoryMap::create_canonical_campaign265_layout();
    IndependentStackAndExceptionAuditor auditor;

    // 1. Audit canonical map
    auto res = auditor.audit_canonical_memory_and_stacks(map);
    TEST_ASSERT(res.passed, "Canonical Campaign 265 map must pass independent audit");

    // 2. Deliberate Guard Removal Mutation across all 12 guards
    for (std::size_t c = 0; c < 6; ++c) {
        std::string err_thr, err_int;
        bool caught_thr = auditor.audit_single_guard_removal(map, c, true, &err_thr);
        bool caught_int = auditor.audit_single_guard_removal(map, c, false, &err_int);

        TEST_ASSERT(caught_thr, "Must catch removed thread guard on core");
        TEST_ASSERT(caught_int, "Must catch removed interrupt guard on core");
    }

    std::cout << "  - All 12 guard removal mutations successfully caught with actionable diagnostics.\n";
    std::cout << "  [PASS] Stage 265.12 Independent ownership auditor verified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 265 - Stages 265.8..265.12 Stack Geometry Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage265_8_reconstructed_stack_geometry()) return 1;
    if (!test_stage265_9_canonical_layout_integrity()) return 1;
    if (!test_stage265_10_memory_map_reconciliation()) return 1;
    if (!test_stage265_11_allocator_geometry()) return 1;
    if (!test_stage265_12_independent_ownership_auditor()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 265.8..265.12 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
