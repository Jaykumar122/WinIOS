#include <iostream>
#include <vector>
#include <cstdint>

#include "memory/memory.h"
#include "memory/kernel_heap_allocator.h"

using namespace winios;
using namespace winios::memory;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_kernel_heap_lifecycle() {
    std::cout << "[Test 1] Stage 210.7: Kernel Heap Allocator Lifecycle & Coalescing...\n";

    Memory mem(0x800000); // 8 MiB guest RAM
    const std::uint64_t heap_base = 0x100000ULL;
    const std::uint64_t heap_size = 0x100000ULL; // 1 MiB heap

    KernelHeapAllocator heap(mem, heap_base, heap_size);
    TEST_CHECK(heap.initialize(), "Kernel heap initialized");

    std::string err;
    TEST_CHECK(heap.validate_heap_integrity(&err), "Initial heap integrity valid");

    // Allocate 64 bytes
    auto p1 = heap.kmalloc(64);
    TEST_CHECK(p1 != 0, "kmalloc(64) succeeded");
    TEST_CHECK((p1 % 16) == 0, "kmalloc(64) returned 16-byte aligned pointer");
    TEST_CHECK(p1 >= heap_base && p1 < heap_base + heap_size, "p1 inside heap bounds");

    // Write test pattern into p1
    mem.write_u64(p1, 0x1122334455667788ULL);
    TEST_CHECK(mem.read_u64(p1) == 0x1122334455667788ULL, "p1 read/write verified");

    // Allocate 128 bytes
    auto p2 = heap.kmalloc(128);
    TEST_CHECK(p2 != 0, "kmalloc(128) succeeded");
    TEST_CHECK((p2 % 16) == 0, "kmalloc(128) returned 16-byte aligned pointer");
    TEST_CHECK(p2 > p1, "p2 positioned after p1");

    // Allocate 256 bytes
    auto p3 = heap.kmalloc(256);
    TEST_CHECK(p3 != 0, "kmalloc(256) succeeded");
    TEST_CHECK(heap.validate_heap_integrity(&err), "Heap integrity valid after 3 allocations");

    // Free p2 (middle block)
    TEST_CHECK(heap.kfree(p2), "kfree(p2) succeeded");
    TEST_CHECK(heap.validate_heap_integrity(&err), "Heap integrity valid after freeing p2");

    // Double free detection
    TEST_CHECK(!heap.kfree(p2), "Double free of p2 rejected");

    // Free p1 (adjacent to p2, should coalesce)
    TEST_CHECK(heap.kfree(p1), "kfree(p1) succeeded");
    TEST_CHECK(heap.validate_heap_integrity(&err), "Heap integrity valid after coalescing p1 and p2");

    // Free p3
    TEST_CHECK(heap.kfree(p3), "kfree(p3) succeeded");
    TEST_CHECK(heap.validate_heap_integrity(&err), "Heap integrity valid after all blocks freed");

    // Reallocate large block to confirm coalesced memory is available
    auto p_large = heap.kmalloc(500);
    TEST_CHECK(p_large != 0, "kmalloc(500) succeeded from coalesced free list");
    TEST_CHECK(heap.kfree(p_large), "kfree(p_large) succeeded");
}

void test_kernel_heap_calloc_and_realloc() {
    std::cout << "[Test 2] Stage 210.7: kcalloc and krealloc Semantics...\n";

    Memory mem(0x800000);
    KernelHeapAllocator heap(mem, 0x100000ULL, 0x100000ULL);
    heap.initialize();

    // kcalloc (10 elements of 16 bytes = 160 bytes, zeroed)
    auto p_calloc = heap.kcalloc(10, 16);
    TEST_CHECK(p_calloc != 0, "kcalloc succeeded");
    for (std::size_t i = 0; i < 160; ++i) {
        if (mem.read_u8(p_calloc + i) != 0) {
            TEST_CHECK(false, "kcalloc memory was not zeroed");
            break;
        }
    }

    // Write marker
    mem.write_u32(p_calloc, 0xCAFEBABE);

    // krealloc to 320 bytes
    auto p_realloc = heap.krealloc(p_calloc, 320);
    TEST_CHECK(p_realloc != 0, "krealloc succeeded");
    TEST_CHECK(mem.read_u32(p_realloc) == 0xCAFEBABE, "Original data preserved across krealloc");

    heap.kfree(p_realloc);
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 210.7 Kernel Heap Tests ===\n";
    test_kernel_heap_lifecycle();
    test_kernel_heap_calloc_and_realloc();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 210.7: All kernel heap tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 210.7: " << g_failures << " tests failed.\n";
        return 1;
    }
}
