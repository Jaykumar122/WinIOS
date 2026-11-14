// ============================================================================
// Stage 195.16: Kernel Heap & Physical Allocator Stress Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "memory/kernel_heap_allocator.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;

// Simple deterministic LCG pseudo-random number generator for reproducible stress
static std::uint32_t lcg_rand(std::uint32_t& state) {
    state = state * 1664525u + 1013904223u;
    return state;
}

// -----------------------------------------------------------------------------
// Test 1: Physical Allocator High-Volume Cycles & Bitmap Consistency
// -----------------------------------------------------------------------------
static bool test_physical_allocator_high_volume_stress() {
    // 8 MB RAM -> 512 pages of 16KB
    Memory mem(0x800000ULL);
    const std::uint64_t managed_base = 0x100000ULL;
    const std::uint64_t managed_size = 0x700000ULL; // 448 pages
    const std::uint64_t bitmap_addr = 0x10000ULL;

    PhysicalPageManager ppm(mem, managed_base, managed_size, bitmap_addr);
    CHECK(ppm.initialize(false), "PPM initialize");

    const auto initial_stats = ppm.get_stats();
    CHECK(initial_stats.total_pages == 448, "Total pages must be 448");
    CHECK(initial_stats.free_pages == 448, "All 448 pages free");

    std::vector<std::uint64_t> active_pages;
    active_pages.reserve(448);

    std::uint32_t rng = 1951601;

    // 600 operations of interleaved allocation and deallocation
    for (std::size_t op = 0; op < 600; ++op) {
        const std::uint32_t action = lcg_rand(rng) % 100;
        if (action < 60 || active_pages.empty()) {
            // 60% chance to allocate (if not full)
            if (active_pages.size() < 448) {
                std::uint64_t p = ppm.allocate_page();
                CHECK(p != 0, "Allocation must succeed when free pages remain");
                CHECK(p >= managed_base && p < managed_base + managed_size, "Allocated address in range");
                CHECK((p % kPhysicalPageSize16KB) == 0, "16KB alignment");
                CHECK(ppm.is_page_allocated(p), "is_page_allocated must be true");
                active_pages.push_back(p);
            }
        } else {
            // 40% chance to free
            const std::size_t idx = lcg_rand(rng) % active_pages.size();
            std::uint64_t p = active_pages[idx];
            CHECK(ppm.free_page(p), "Free must succeed");
            CHECK(!ppm.is_page_allocated(p), "is_page_allocated must be false after free");
            active_pages[idx] = active_pages.back();
            active_pages.pop_back();
        }

        // Verify stats consistency at every step
        auto s = ppm.get_stats();
        CHECK(s.allocated_pages == active_pages.size(), "Allocated pages matches active list");
        CHECK(s.free_pages == s.total_pages - active_pages.size(), "Free pages matches formula");
    }

    // Verify raw bitmap bits in guest RAM directly match active_pages count
    std::size_t bits_set = 0;
    for (std::size_t b = 0; b < initial_stats.bitmap_size_bytes; ++b) {
        std::uint8_t byte_val = mem.read_u8(static_cast<std::size_t>(bitmap_addr + b));
        for (int bit = 0; bit < 8; ++bit) {
            if ((byte_val >> bit) & 1) ++bits_set;
        }
    }
    CHECK(bits_set == active_pages.size(), "Raw bitmap bits set must match active_pages size");

    // Clean up remaining allocations
    for (std::uint64_t p : active_pages) {
        CHECK(ppm.free_page(p), "Cleanup free");
    }
    CHECK(ppm.get_stats().free_pages == initial_stats.total_pages, "All pages free after cleanup");

    std::cout << "[PASS] test_physical_allocator_high_volume_stress (600 ops, verified bitmap integrity)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 2: Physical Allocator Fragmentation, Exhaustion, and Contiguity Checks
// -----------------------------------------------------------------------------
static bool test_physical_allocator_fragmentation_and_exhaustion() {
    Memory mem(0x400000ULL);
    const std::uint64_t managed_base = 0x100000ULL;
    const std::uint64_t managed_size = 0x100000ULL; // 64 pages (1MB)
    const std::uint64_t bitmap_addr = 0x10000ULL;

    PhysicalPageManager ppm(mem, managed_base, managed_size, bitmap_addr);
    CHECK(ppm.initialize(false), "PPM initialize");

    std::vector<std::uint64_t> pages;
    pages.reserve(64);

    // 1. Allocate until exhaustion (exactly 64 pages)
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint64_t p = ppm.allocate_page();
        CHECK(p != 0, "Must allocate page i");
        pages.push_back(p);
    }
    CHECK(ppm.get_stats().free_pages == 0, "Free pages must be 0 after exhaustion");
    CHECK(ppm.allocate_page() == 0, "Allocation past exhaustion must return 0");
    CHECK(ppm.allocate_pages(2) == 0, "Multi-page allocation on exhausted pool must return 0");

    // 2. Free every even index -> creates checkerboard fragmentation
    // [Free, Alloc, Free, Alloc, Free, Alloc...]
    for (std::size_t i = 0; i < 64; i += 2) {
        CHECK(ppm.free_page(pages[i]), "Free even index page");
    }
    CHECK(ppm.get_stats().free_pages == 32, "Exactly 32 free pages");

    // 3. Contiguous allocation of 2 pages MUST FAIL because every free page is bounded by an allocated page!
    CHECK(ppm.allocate_pages(2) == 0, "Contiguous 2-page alloc must fail on fragmented pool");

    // 4. Single-page allocation MUST SUCCEED by claiming a fragmented slot
    std::uint64_t reclaimed = ppm.allocate_page();
    CHECK(reclaimed == pages[0], "First fragmented slot 0 must be reclaimed");
    CHECK(ppm.get_stats().free_pages == 31, "Free count down by 1");

    // 5. Free all remaining pages
    CHECK(ppm.free_page(reclaimed), "Free reclaimed slot");
    for (std::size_t i = 1; i < 64; i += 2) {
        CHECK(ppm.free_page(pages[i]), "Free odd index page");
    }
    CHECK(ppm.get_stats().free_pages == 64, "All 64 pages free again");

    // 6. Now large contiguous allocation of 32 pages must succeed
    std::uint64_t span32 = ppm.allocate_pages(32);
    CHECK(span32 == managed_base, "32-page contiguous block allocated from base");
    CHECK(ppm.free_pages(span32, 32), "Free 32-page span");

    std::cout << "[PASS] test_physical_allocator_fragmentation_and_exhaustion\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 3: Physical Allocator Double-Free & Out-of-Bounds Rejection
// -----------------------------------------------------------------------------
static bool test_physical_allocator_rejection() {
    Memory mem(0x400000ULL);
    PhysicalPageManager ppm(mem, 0x100000ULL, 0x100000ULL, 0x10000ULL);
    ppm.initialize(false);

    std::uint64_t p = ppm.allocate_page();
    CHECK(p != 0, "Allocate");
    CHECK(ppm.free_page(p), "First free succeeds");

    // Double free on the same address must return false
    CHECK(!ppm.free_page(p), "Double free must be rejected");

    // Free of unmanaged out-of-bounds address must return false
    CHECK(!ppm.free_page(0x0ULL), "Free address below base rejected");
    CHECK(!ppm.free_page(0x300000ULL), "Free address above size rejected");
    CHECK(!ppm.free_page(0x100001ULL), "Free unaligned address rejected");

    std::cout << "[PASS] test_physical_allocator_rejection (Double-free & out-of-bounds guarded)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 4: Kernel Heap Mixed-Size Batches, 16-Byte Alignment & Data Preservation
// -----------------------------------------------------------------------------
static bool test_kernel_heap_mixed_size_stress() {
    Memory mem(0x800000ULL); // 8MB
    const std::uint64_t heap_base = 0x200000ULL;
    const std::uint64_t heap_size = 0x400000ULL; // 4MB heap

    KernelHeapAllocator heap(mem, heap_base, heap_size);
    CHECK(heap.initialize(), "Heap init");

    struct AllocRecord {
        std::uint64_t ptr{0};
        std::size_t size{0};
        std::uint32_t magic{0};
    };

    std::vector<AllocRecord> records;
    records.reserve(128);

    std::uint32_t rng = 20261114;

    // Allocate 100 blocks of various sizes (16B, 64B, 256B, 1024B, 4096B)
    const std::vector<std::size_t> size_classes = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096};

    for (std::size_t i = 0; i < 100; ++i) {
        std::size_t sz = size_classes[lcg_rand(rng) % size_classes.size()];
        std::uint64_t ptr = heap.kmalloc(sz);
        CHECK(ptr != 0, "kmalloc must succeed");
        CHECK((ptr % kHeapAlignment) == 0, "Pointer must be 16-byte aligned");
        CHECK(ptr >= heap_base + kHeapHeaderSize, "Pointer inside heap bounds");

        // Write a recognizable canary pattern into the entire payload
        std::uint32_t cookie = static_cast<std::uint32_t>(0xA0000000u | (i & 0xFFFFFFu));
        for (std::size_t offset = 0; offset + 4 <= sz; offset += 4) {
            mem.write_u32(static_cast<std::size_t>(ptr + offset), cookie);
        }

        records.push_back({ptr, sz, cookie});
    }

    std::string err;
    CHECK(heap.validate_heap_integrity(&err), "Heap integrity after 100 allocations");

    // Verify all 100 payload patterns remain completely uncorrupted (no overlapping blocks!)
    for (const auto& rec : records) {
        for (std::size_t offset = 0; offset + 4 <= rec.size; offset += 4) {
            std::uint32_t val = mem.read_u32(static_cast<std::size_t>(rec.ptr + offset));
            CHECK(val == rec.magic, "Canary magic must match without corruption");
        }
    }

    // Free half of the allocations in random order
    for (std::size_t i = 0; i < 50; ++i) {
        const std::size_t idx = lcg_rand(rng) % records.size();
        CHECK(heap.kfree(records[idx].ptr), "kfree record");
        records[idx] = records.back();
        records.pop_back();
    }

    CHECK(heap.validate_heap_integrity(&err), "Heap integrity after 50 frees");

    // Verify the remaining 50 records are still untouched
    for (const auto& rec : records) {
        for (std::size_t offset = 0; offset + 4 <= rec.size; offset += 4) {
            std::uint32_t val = mem.read_u32(static_cast<std::size_t>(rec.ptr + offset));
            CHECK(val == rec.magic, "Canary magic intact after interleaved frees");
        }
    }

    // Free the remaining blocks
    for (const auto& rec : records) {
        CHECK(heap.kfree(rec.ptr), "Free remaining");
    }

    CHECK(heap.validate_heap_integrity(&err), "Heap integrity after full cleanup");
    CHECK(heap.get_stats().active_allocations == 0, "0 active allocations after cleanup");

    std::cout << "[PASS] test_kernel_heap_mixed_size_stress (100 allocations, verified 16-byte alignment and payload integrity)\n";
    return true;
}

// -----------------------------------------------------------------------------
// Test 5: Kernel Heap Free-Block Coalescing & Realloc
// -----------------------------------------------------------------------------
static bool test_kernel_heap_coalescing_and_realloc() {
    Memory mem(0x400000ULL);
    const std::uint64_t heap_base = 0x200000ULL;
    const std::uint64_t heap_size = 0x100000ULL; // 1MB

    KernelHeapAllocator heap(mem, heap_base, heap_size);
    CHECK(heap.initialize(), "Heap init");

    // 1. Allocate 5 sequential blocks: A, B, C, D, E
    const std::uint64_t pA = heap.kmalloc(128);
    const std::uint64_t pB = heap.kmalloc(256);
    const std::uint64_t pC = heap.kmalloc(512);
    const std::uint64_t pD = heap.kmalloc(256);
    const std::uint64_t pE = heap.kmalloc(128);

    CHECK(pA != 0 && pB != 0 && pC != 0 && pD != 0 && pE != 0, "5 blocks allocated");

    // Free B, D, then C (middle) -> B, C, D should coalesce into one large block
    CHECK(heap.kfree(pB), "Free B");
    CHECK(heap.kfree(pD), "Free D");
    CHECK(heap.kfree(pC), "Free C");

    std::string err;
    CHECK(heap.validate_heap_integrity(&err), "Integrity after coalescing");

    // Now allocate a block of 1000 bytes: it must fit into the combined B+C+D free region!
    const std::uint64_t pMerged = heap.kmalloc(1000);
    CHECK(pMerged != 0, "Alloc 1000B in coalesced hole");
    CHECK(pMerged == pB, "Merged block must be placed at address of B (start of coalesced hole)");

    // Clean up
    CHECK(heap.kfree(pA), "Free A");
    CHECK(heap.kfree(pE), "Free E");
    CHECK(heap.kfree(pMerged), "Free Merged");

    // 2. Test krealloc: Expansion & Contraction with data preservation
    const std::uint64_t r1 = heap.kmalloc(64);
    CHECK(r1 != 0, "Initial alloc 64B");
    for (std::size_t i = 0; i < 64; ++i) {
        mem.write_u8(static_cast<std::size_t>(r1 + i), static_cast<std::uint8_t>(0xC0 + (i & 0x0F)));
    }

    // Expand to 256 bytes
    const std::uint64_t r2 = heap.krealloc(r1, 256);
    CHECK(r2 != 0, "krealloc expand to 256B");
    CHECK((r2 % kHeapAlignment) == 0, "Realloc 16-byte alignment");

    // Verify first 64 bytes were preserved!
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint8_t b = mem.read_u8(static_cast<std::size_t>(r2 + i));
        CHECK(b == static_cast<std::uint8_t>(0xC0 + (i & 0x0F)), "Data preserved across krealloc expansion");
    }

    // Contract to 32 bytes
    const std::uint64_t r3 = heap.krealloc(r2, 32);
    CHECK(r3 != 0, "krealloc contract to 32B");
    for (std::size_t i = 0; i < 32; ++i) {
        std::uint8_t b = mem.read_u8(static_cast<std::size_t>(r3 + i));
        CHECK(b == static_cast<std::uint8_t>(0xC0 + (i & 0x0F)), "Data preserved across krealloc contraction");
    }

    CHECK(heap.kfree(r3), "Free final realloc block");
    CHECK(heap.validate_heap_integrity(&err), "Heap clean at finish");

    std::cout << "[PASS] test_kernel_heap_coalescing_and_realloc\n";
    return true;
}

int main() {
    std::cout << "Running Stage 195.16 Kernel Heap & Physical Allocator Stress Tests...\n";

    if (!test_physical_allocator_high_volume_stress()) return 1;
    if (!test_physical_allocator_fragmentation_and_exhaustion()) return 1;
    if (!test_physical_allocator_rejection()) return 1;
    if (!test_kernel_heap_mixed_size_stress()) return 1;
    if (!test_kernel_heap_coalescing_and_realloc()) return 1;

    std::cout << "\nAll Stage 195.16 Allocator Stress tests PASSED successfully.\n";
    return 0;
}
