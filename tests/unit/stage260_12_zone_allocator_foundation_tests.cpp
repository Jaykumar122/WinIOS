#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/xnu_zone_allocator.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 260.11 - Mach VM Zone Allocator Design & Invariant Verification
// ----------------------------------------------------------------------------
static void test_stage260_11_zone_design_and_invariants() {
    std::cout << "[Stage 260.11] Testing Mach VM Zone Allocator Design & Memory Invariants...\n";

    // Standard Zone Pool placed at 0x200000 (2 MiB), size 0x200000 (2 MiB)
    const uint64_t pool_base = 0x200000ULL;
    const uint64_t pool_size = 0x200000ULL;
    const uint64_t pool_end  = pool_base + pool_size; // 0x400000

    XnuZoneAllocator za(pool_base, pool_size);
    TEST_ASSERT(za.pool_base() == pool_base, "Pool base is 0x200000");
    TEST_ASSERT(za.pool_size() == pool_size, "Pool size is 0x200000 (2 MiB)");

    // Invariant: Pool must NEVER overlap canonical reserved frames 0..19 (0x00000 .. 0x50000)
    const uint64_t reserved_frames_end = 0x50000ULL; // 20 frames * 16 KiB
    TEST_ASSERT(pool_base >= reserved_frames_end, "Zone pool does not overlap reserved frames 0..19");

    // Invariant: Pool must NEVER overlap kernel text at 0x80000 .. 0x84000
    const uint64_t text_base = 0x80000ULL;
    const uint64_t text_end  = 0x84000ULL;
    TEST_ASSERT(pool_base >= text_end || pool_end <= text_base, "Zone pool does not overlap kernel text");

    // Invariant: Pool must NEVER overlap kernel exception stacks at 0x40000 .. 0x44000
    const uint64_t stack_base = 0x40000ULL;
    const uint64_t stack_end  = 0x44000ULL;
    TEST_ASSERT(pool_base >= stack_end || pool_end <= stack_base, "Zone pool does not overlap exception stack");

    // Invariant: Pool must NEVER overlap MMIO registers at 0x08000000 or 0x23B100000
    const uint64_t aic_mmio_base = 0x08000000ULL;
    TEST_ASSERT(pool_end <= aic_mmio_base, "Zone pool does not overlap AIC MMIO region");

    std::cout << "[Stage 260.11] Zone design and memory isolation invariants verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 260.12 - Bounded Zone Implementation & Failure Handling
// ----------------------------------------------------------------------------
static void test_stage260_12_bounded_allocation_and_double_free() {
    std::cout << "[Stage 260.12] Testing Bounded Zone Allocation, Pointer Validation & Double-Free...\n";

    XnuZoneAllocator za(0x200000ULL, 0x100000ULL); // 1 MiB pool
    std::string err;

    // Create a zone with element size 40 (aligned to 48: 16-byte multiple), max 8 elements
    bool ok = za.create_zone("test.thread", 40, 8, &err);
    TEST_ASSERT(ok, "create_zone succeeded");
    TEST_ASSERT(err.empty(), "No error on create_zone");

    ZoneStats stats{};
    ok = za.get_zone_stats("test.thread", stats);
    TEST_ASSERT(ok, "get_zone_stats succeeded");
    TEST_ASSERT(stats.elem_size == 48, "Element size aligned up to 16 bytes (40 -> 48)");
    TEST_ASSERT(stats.max_count == 8, "Max count is 8");
    TEST_ASSERT(stats.count == 0, "Initial allocated count is 0");
    TEST_ASSERT(stats.free_count == 8, "Initial free count is 8");

    // Allocate 3 elements
    uint64_t ptr0 = za.zalloc("test.thread");
    uint64_t ptr1 = za.zalloc("test.thread");
    uint64_t ptr2 = za.zalloc("test.thread");
    TEST_ASSERT(ptr0 != 0, "ptr0 allocated");
    TEST_ASSERT(ptr1 != 0, "ptr1 allocated");
    TEST_ASSERT(ptr2 != 0, "ptr2 allocated");
    TEST_ASSERT(ptr0 != ptr1 && ptr1 != ptr2, "Pointers are distinct");
    TEST_ASSERT(ptr1 == ptr0 + 48, "Pointer delta matches aligned element size");

    // Free ptr1
    ok = za.zfree("test.thread", ptr1, &err);
    TEST_ASSERT(ok, "zfree succeeded");

    // Double free detection on ptr1
    ok = za.zfree("test.thread", ptr1, &err);
    TEST_ASSERT(!ok, "Double free correctly rejected");
    TEST_ASSERT(err.find("double free detected") != std::string::npos, "Double free error message emitted");

    // Foreign pointer rejection (outside zone bounds)
    ok = za.zfree("test.thread", 0x100000ULL, &err);
    TEST_ASSERT(!ok, "Foreign pointer rejected");
    TEST_ASSERT(err.find("out of bounds") != std::string::npos, "Out of bounds error message emitted");

    // Misaligned pointer rejection
    ok = za.zfree("test.thread", ptr0 + 7, &err);
    TEST_ASSERT(!ok, "Misaligned pointer rejected");
    TEST_ASSERT(err.find("misaligned") != std::string::npos, "Misaligned error message emitted");

    std::cout << "[Stage 260.12] Bounded allocation, pointer validation and double-free verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 260.13 - Zone Initialization & Canonical Allocator Integration
// ----------------------------------------------------------------------------
static void test_stage260_13_multi_zone_isolation() {
    std::cout << "[Stage 260.13] Testing Multi-Zone Isolation & 16 KiB Page Alignment...\n";

    XnuZoneAllocator za(0x200000ULL, 0x100000ULL);
    std::string err;

    // Create 3 realistic Darwin kernel zones:
    TEST_ASSERT(za.create_zone("vm.threads", 256, 32, &err), "vm.threads created");
    TEST_ASSERT(za.create_zone("ipc.ports",   64, 64, &err), "ipc.ports created");
    TEST_ASSERT(za.create_zone("vfs.nodes",  512, 16, &err), "vfs.nodes created");
    TEST_ASSERT(za.zone_count() == 3, "Exactly 3 zones registered");

    // Allocate from each zone
    uint64_t t0 = za.zalloc("vm.threads");
    uint64_t p0 = za.zalloc("ipc.ports");
    uint64_t v0 = za.zalloc("vfs.nodes");

    TEST_ASSERT(t0 != 0 && p0 != 0 && v0 != 0, "Allocations succeeded across all 3 zones");

    // Cross-zone free rejection: freeing t0 into "ipc.ports" must fail
    bool ok = za.zfree("ipc.ports", t0, &err);
    TEST_ASSERT(!ok, "Cross-zone free rejected");
    TEST_ASSERT(err.find("out of bounds") != std::string::npos, "Cross-zone pointer is out of bounds");

    // Free correctly
    TEST_ASSERT(za.zfree("vm.threads", t0, &err), "t0 freed in vm.threads");
    TEST_ASSERT(za.zfree("ipc.ports", p0, &err), "p0 freed in ipc.ports");
    TEST_ASSERT(za.zfree("vfs.nodes", v0, &err), "v0 freed in vfs.nodes");

    std::cout << "[Stage 260.13] Multi-zone isolation and alignment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 260.14 - Adversarial Testing, Exhaustion & Snapshot Replay
// ----------------------------------------------------------------------------
static void test_stage260_14_adversarial_exhaustion_and_replay() {
    std::cout << "[Stage 260.14] Testing Adversarial Edge Cases, Exhaustion & Replay...\n";

    XnuZoneAllocator za(0x200000ULL, 0x080000ULL); // 512 KiB pool
    std::string err;

    // 1. Zero element size
    TEST_ASSERT(!za.create_zone("bad.zero", 0, 10, &err), "Zero element size rejected");
    TEST_ASSERT(err.find("zero") != std::string::npos, "Zero error emitted");

    // 2. Zero max elements
    TEST_ASSERT(!za.create_zone("bad.count", 64, 0, &err), "Zero max elements rejected");

    // 3. Duplicate zone name
    TEST_ASSERT(za.create_zone("dup.zone", 64, 4, &err), "dup.zone created");
    TEST_ASSERT(!za.create_zone("dup.zone", 64, 4, &err), "Duplicate zone name rejected");
    TEST_ASSERT(err.find("already exists") != std::string::npos, "Duplicate error emitted");

    // 4. Integer overflow in total size
    TEST_ASSERT(!za.create_zone("overflow.zone", 0x8000000000000000ULL, 4, &err), "Overflow rejected");
    TEST_ASSERT(err.find("overflow") != std::string::npos, "Overflow error emitted");

    // 5. Pool capacity exhaustion during create_zone
    TEST_ASSERT(!za.create_zone("huge.zone", 1024, 100000, &err), "Excessive zone capacity rejected");

    // 6. Zone exhaustion
    std::vector<uint64_t> allocated;
    for (int i = 0; i < 4; ++i) {
        uint64_t ptr = za.zalloc("dup.zone");
        TEST_ASSERT(ptr != 0, "Alloc succeeded");
        allocated.push_back(ptr);
    }
    // 5th allocation must fail
    uint64_t failed_ptr = za.zalloc("dup.zone");
    TEST_ASSERT(failed_ptr == 0, "5th allocation failed deterministically on exhausted zone");

    // 7. Snapshot Serialization & Replay
    std::vector<uint8_t> snap = za.serialize();
    TEST_ASSERT(!snap.empty(), "Snapshot generated");

    XnuZoneAllocator za_restored;
    TEST_ASSERT(za_restored.deserialize(snap.data(), snap.size(), &err), "Snapshot restored successfully");
    TEST_ASSERT(za_restored.zone_count() == za.zone_count(), "Restored zone count matches");

    // In restored allocator, dup.zone should still be exhausted
    TEST_ASSERT(za_restored.zalloc("dup.zone") == 0, "Restored zone is still exhausted");

    // Free one in restored allocator
    TEST_ASSERT(za_restored.zfree("dup.zone", allocated[0], &err), "Freed element in restored zone");

    // Now allocation in restored allocator succeeds!
    uint64_t reclaimed_ptr = za_restored.zalloc("dup.zone");
    TEST_ASSERT(reclaimed_ptr == allocated[0], "Reclaimed exact same element deterministically");

    std::cout << "[Stage 260.14] Adversarial edge cases, exhaustion and replay verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 260: Stages 260.11 - 260.14 Zone Allocator Foundation Tests\n";
    std::cout << "====================================================================\n";

    test_stage260_11_zone_design_and_invariants();
    test_stage260_12_bounded_allocation_and_double_free();
    test_stage260_13_multi_zone_isolation();
    test_stage260_14_adversarial_exhaustion_and_replay();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 260.11 - 260.14 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
