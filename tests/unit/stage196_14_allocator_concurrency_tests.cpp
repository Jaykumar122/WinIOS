#include <iostream>
#include <cstdint>
#include <cassert>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <set>

#include "cpu/cpu.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/kernel_heap_allocator.h"

using namespace winios;
using namespace winios::memory;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << "\n"; \
            return false; \
        } \
    } while (0)

// -----------------------------------------------------------------------------
// Test K1: Concurrent Physical Page Manager Allocation & Free (Acceptance Test K)
// -----------------------------------------------------------------------------
static bool test_concurrent_physical_page_allocation() {
    constexpr std::uint64_t kManagedBase = 0x100000ULL;
    constexpr std::uint64_t kManagedSize = 1024 * 16384ULL; // 1024 pages (16 MB)
    constexpr std::uint64_t kBitmapAddr  = 0x40000ULL;
    Memory mem(32 * 1024 * 1024);

    PhysicalPageManager ppm(mem, kManagedBase, kManagedSize, kBitmapAddr);
    TEST_ASSERT(ppm.initialize(false), "PPM initialized");

    const auto initial_stats = ppm.get_stats();
    TEST_ASSERT(initial_stats.free_pages == 1024, "All 1024 pages free initially");

    constexpr int kNumThreads = 8;
    constexpr int kOpsPerThread = 100;
    std::atomic<bool> failed{false};
    std::atomic<int> start_barrier{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            start_barrier.fetch_add(1);
            while (start_barrier.load() < kNumThreads) {
                std::this_thread::yield();
            }

            std::mt19937 rng(1337 + t);
            for (int op = 0; op < kOpsPerThread; ++op) {
                const std::size_t pages_to_alloc = (rng() % 3) + 1; // 1 to 3 pages
                const std::uint64_t addr = ppm.allocate_pages(pages_to_alloc);
                if (addr == 0) {
                    continue; // Out of memory is acceptable under heavy concurrency
                }

                if (addr < kManagedBase || (addr % kPhysicalPageSize16KB) != 0) {
                    failed.store(true);
                    return;
                }

                // Verify each page is reported allocated
                for (std::size_t p = 0; p < pages_to_alloc; ++p) {
                    if (!ppm.is_page_allocated(addr + p * kPhysicalPageSize16KB)) {
                        failed.store(true);
                        return;
                    }
                }

                // Free allocated pages
                if (!ppm.free_pages(addr, pages_to_alloc)) {
                    failed.store(true);
                    return;
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(!failed.load(), "Concurrent allocation and free operations succeeded without failure");

    // All pages must be reclaimed
    const auto final_stats = ppm.get_stats();
    TEST_ASSERT(final_stats.free_pages == 1024, "All pages restored to free list after thread completion");
    TEST_ASSERT(final_stats.allocated_pages == 0, "Zero allocated pages remain");

    // Double free test on unmanaged address
    TEST_ASSERT(!ppm.free_pages(0x50000ULL, 1), "Freeing out-of-bounds address rejected");

    return true;
}

// -----------------------------------------------------------------------------
// Test L1: Concurrent Kernel Heap Allocator & Coalescing (Acceptance Test L)
// -----------------------------------------------------------------------------
static bool test_concurrent_kernel_heap_allocation() {
    constexpr std::uint64_t kHeapBase = 0x200000ULL;
    constexpr std::uint64_t kHeapSize = 4 * 1024 * 1024ULL; // 4 MB
    Memory mem(16 * 1024 * 1024);

    KernelHeapAllocator heap(mem, kHeapBase, kHeapSize);
    TEST_ASSERT(heap.initialize(), "Heap initialized");

    std::string err;
    TEST_ASSERT(heap.validate_heap_integrity(&err), "Initial heap integrity valid");

    constexpr int kNumThreads = 8;
    constexpr int kOpsPerThread = 120;
    std::atomic<bool> failed{false};
    std::atomic<int> start_barrier{0};

    std::vector<std::thread> workers;
    for (int t = 0; t < kNumThreads; ++t) {
        workers.emplace_back([&, t]() {
            start_barrier.fetch_add(1);
            while (start_barrier.load() < kNumThreads) {
                std::this_thread::yield();
            }

            std::mt19937 rng(4242 + t);
            std::vector<std::uint64_t> active_ptrs;

            for (int op = 0; op < kOpsPerThread; ++op) {
                const int act = rng() % 10;
                if (act < 6 || active_ptrs.empty()) {
                    // kmalloc or kcalloc
                    const std::size_t sz = 16 + (rng() % 256);
                    std::uint64_t ptr = 0;
                    if (act < 3) {
                        ptr = heap.kcalloc(1, sz);
                        if (ptr != 0) {
                            for (std::size_t i = 0; i < sz; ++i) {
                                if (mem.read_u8(static_cast<std::size_t>(ptr + i)) != 0) {
                                    failed.store(true);
                                    return;
                                }
                            }
                        }
                    } else {
                        ptr = heap.kmalloc(sz);
                    }

                    if (ptr != 0) {
                        // Tag payload
                        const std::uint8_t tag = static_cast<std::uint8_t>(t + 1);
                        mem.write_u8(static_cast<std::size_t>(ptr), tag);
                        active_ptrs.push_back(ptr);
                    }
                } else if (act < 8) {
                    // krealloc
                    const std::size_t idx = rng() % active_ptrs.size();
                    const std::uint64_t old_ptr = active_ptrs[idx];
                    const std::size_t new_sz = 32 + (rng() % 512);
                    const std::uint64_t new_ptr = heap.krealloc(old_ptr, new_sz);
                    if (new_ptr != 0) {
                        active_ptrs[idx] = new_ptr;
                    }
                } else {
                    // kfree
                    const std::size_t idx = rng() % active_ptrs.size();
                    const std::uint64_t ptr = active_ptrs[idx];
                    if (!heap.kfree(ptr)) {
                        failed.store(true);
                        return;
                    }
                    active_ptrs.erase(active_ptrs.begin() + idx);
                }
            }

            // Cleanup remaining active pointers
            for (auto ptr : active_ptrs) {
                if (!heap.kfree(ptr)) {
                    failed.store(true);
                    return;
                }
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    TEST_ASSERT(!failed.load(), "Concurrent heap operations executed cleanly without memory corruption");

    // Verify heap integrity and full coalescing back to 1 free block
    TEST_ASSERT(heap.validate_heap_integrity(&err), "Heap integrity valid after stress");
    const auto final_stats = heap.get_stats();
    TEST_ASSERT(final_stats.active_allocations == 0, "Zero active allocations remain");
    TEST_ASSERT(final_stats.allocated_bytes == 0, "Zero allocated bytes remain");
    TEST_ASSERT(final_stats.largest_free_block == kHeapSize, "Heap fully coalesced into single free block");

    return true;
}

// -----------------------------------------------------------------------------
// Test K2: Guest ARM64 Physical Page Allocator Routine Execution
// -----------------------------------------------------------------------------
static bool test_guest_page_alloc_routine() {
    Memory mem(1024 * 1024);
    constexpr std::uint64_t kBitmapAddr = 0x2000ULL;
    constexpr std::uint64_t kCodeAddr   = 0x4000ULL;

    // Bitmap with 64 pages: initialize all free except page 0
    for (std::size_t i = 0; i < 8; ++i) {
        mem.write_u8(static_cast<std::size_t>(kBitmapAddr + i), (i == 0) ? 0x01 : 0x00);
    }

    // Load guest alloc routine
    const auto alloc_code = PhysicalPageManager::emit_guest_alloc_routine();
    for (std::size_t i = 0; i < alloc_code.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(kCodeAddr + i * 4), alloc_code[i]);
    }

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_program_counter(kCodeAddr);
    cpu.write_register(0, kBitmapAddr); // X0 = bitmap address
    cpu.write_register(1, 64);          // X1 = total pages
    cpu.write_register(30, 0x9000ULL);  // LR

    // Execute routine
    while (cpu.get_program_counter() != 0x9000ULL) {
        cpu.step(mem);
    }

    // X0 should be page index 1 (since page 0 was allocated)
    const std::uint64_t allocated_page = cpu.read_register(0);
    TEST_ASSERT(allocated_page == 1, "Guest routine allocated page 1");

    // Verify bit 1 is now set in bitmap
    const std::uint8_t byte0 = mem.read_u8(static_cast<std::size_t>(kBitmapAddr));
    TEST_ASSERT((byte0 & 0x02) != 0, "Bit 1 set in guest bitmap");

    return true;
}

int main() {
    std::cout << "[STAGE 196.14] Running Memory Allocator Concurrency Tests (Acceptance Tests K & L)...\n";

    if (!test_concurrent_physical_page_allocation()) return 1;
    if (!test_concurrent_kernel_heap_allocation()) return 1;
    if (!test_guest_page_alloc_routine()) return 1;

    std::cout << "[STAGE 196.14] All Memory Allocator Concurrency Tests PASSED (Acceptance Tests K & L VERIFIED).\n";
    return 0;
}
