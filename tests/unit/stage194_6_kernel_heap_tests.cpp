// ============================================================================
// Stage 194.6: Guest Kernel Heap Allocator (kmalloc / kfree) Tests
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "memory/kernel_heap_allocator.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;

static bool test_heap_initialization_and_alignment() {
    Memory mem(4 * 1024 * 1024); // 4MB RAM
    const std::uint64_t heap_base = 0x200000;
    const std::uint64_t heap_size = 0x100000; // 1MB

    KernelHeapAllocator allocator(mem, heap_base, heap_size);
    CHECK(allocator.initialize(), "Heap initialization must succeed");

    std::string err;
    CHECK(allocator.validate_heap_integrity(&err), "Initial heap must be intact");

    KernelHeapStats stats = allocator.get_stats();
    CHECK(stats.total_heap_bytes == heap_size, "Total heap bytes must match configuration");
    CHECK(stats.active_allocations == 0, "No active allocations initially");
    CHECK(stats.free_bytes == heap_size, "Entire heap must initially be free");

    // Allocate varied sizes
    const std::uint64_t p1 = allocator.kmalloc(1);
    const std::uint64_t p2 = allocator.kmalloc(23);
    const std::uint64_t p3 = allocator.kmalloc(64);

    CHECK(p1 != 0, "p1 allocation must succeed");
    CHECK(p2 != 0, "p2 allocation must succeed");
    CHECK(p3 != 0, "p3 allocation must succeed");

    // Verify 16-byte alignment
    CHECK((p1 % kHeapAlignment) == 0, "p1 must be 16-byte aligned");
    CHECK((p2 % kHeapAlignment) == 0, "p2 must be 16-byte aligned");
    CHECK((p3 % kHeapAlignment) == 0, "p3 must be 16-byte aligned");

    CHECK(p1 >= heap_base + kHeapHeaderSize, "p1 must be within heap");
    CHECK(p2 > p1, "p2 must be placed after p1");
    CHECK(p3 > p2, "p3 must be placed after p2");

    stats = allocator.get_stats();
    CHECK(stats.active_allocations == 3, "Active allocations must equal 3");
    CHECK(stats.total_alloc_operations == 3, "Total alloc ops must equal 3");
    CHECK(allocator.validate_heap_integrity(&err), "Heap must remain integral after allocations");

    std::cout << "[PASS] test_heap_initialization_and_alignment\n";
    return true;
}

static bool test_kmalloc_kfree_lifecycle() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t heap_base = 0x200000;
    const std::uint64_t heap_size = 0x80000; // 512KB

    KernelHeapAllocator allocator(mem, heap_base, heap_size);
    CHECK(allocator.initialize(), "Initialization must succeed");

    const std::uint64_t a = allocator.kmalloc(128);
    const std::uint64_t b = allocator.kmalloc(256);
    const std::uint64_t c = allocator.kmalloc(512);

    CHECK(a != 0 && b != 0 && c != 0, "All 3 blocks must allocate");

    // Write patterns into buffers
    for (std::size_t i = 0; i < 128; ++i) mem.write_u8(static_cast<std::size_t>(a + i), 0xAA);
    for (std::size_t i = 0; i < 256; ++i) mem.write_u8(static_cast<std::size_t>(b + i), 0xBB);
    for (std::size_t i = 0; i < 512; ++i) mem.write_u8(static_cast<std::size_t>(c + i), 0xCC);

    // Verify patterns
    for (std::size_t i = 0; i < 128; ++i) CHECK(mem.read_u8(static_cast<std::size_t>(a + i)) == 0xAA, "a integrity");
    for (std::size_t i = 0; i < 256; ++i) CHECK(mem.read_u8(static_cast<std::size_t>(b + i)) == 0xBB, "b integrity");
    for (std::size_t i = 0; i < 512; ++i) CHECK(mem.read_u8(static_cast<std::size_t>(c + i)) == 0xCC, "c integrity");

    // Free block b
    CHECK(allocator.kfree(b), "Freeing b must succeed");
    KernelHeapStats stats = allocator.get_stats();
    CHECK(stats.active_allocations == 2, "Active allocations must now be 2");
    CHECK(stats.total_free_operations == 1, "Total free ops must be 1");

    // Reallocate 256 bytes - should reuse b's space
    const std::uint64_t b2 = allocator.kmalloc(256);
    CHECK(b2 == b, "First-fit allocator should reuse block b's freed address");

    // Free remaining
    CHECK(allocator.kfree(a), "Free a");
    CHECK(allocator.kfree(b2), "Free b2");
    CHECK(allocator.kfree(c), "Free c");

    stats = allocator.get_stats();
    CHECK(stats.active_allocations == 0, "Active allocations must return to 0");

    std::cout << "[PASS] test_kmalloc_kfree_lifecycle\n";
    return true;
}

static bool test_forward_coalescing() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t heap_base = 0x200000;
    const std::uint64_t heap_size = 0x40000; // 256KB

    KernelHeapAllocator allocator(mem, heap_base, heap_size);
    CHECK(allocator.initialize(), "Initialization must succeed");

    const std::uint64_t b1 = allocator.kmalloc(64);
    const std::uint64_t b2 = allocator.kmalloc(64);
    const std::uint64_t b3 = allocator.kmalloc(64);
    const std::uint64_t b4 = allocator.kmalloc(64);

    CHECK(b1 != 0 && b2 != 0 && b3 != 0 && b4 != 0, "All 4 blocks allocated");

    // Free b2 first, then b1.
    // When b1 is freed, it sees b2 is free right after it, and merges forward.
    CHECK(allocator.kfree(b2), "Free b2");
    CHECK(allocator.kfree(b1), "Free b1 and trigger forward coalesce");

    // Now request 140 bytes (which requires a block larger than 64+16=80 bytes)
    // It should fit directly into the coalesced block starting at b1!
    const std::uint64_t merged_alloc = allocator.kmalloc(140);
    CHECK(merged_alloc == b1, "Merged free block must accommodate larger request at b1");

    CHECK(allocator.kfree(merged_alloc), "Free merged block");
    CHECK(allocator.kfree(b3), "Free b3");
    CHECK(allocator.kfree(b4), "Free b4");

    std::string err;
    CHECK(allocator.validate_heap_integrity(&err), "Heap must remain valid after coalescing operations");

    std::cout << "[PASS] test_forward_coalescing\n";
    return true;
}

static bool test_krealloc_and_kcalloc() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t heap_base = 0x200000;
    const std::uint64_t heap_size = 0x40000;

    KernelHeapAllocator allocator(mem, heap_base, heap_size);
    CHECK(allocator.initialize(), "Init heap");

    // Test kcalloc: 16 items of 16 bytes = 256 bytes
    // Pre-fill memory area with 0xEE to verify zeroing
    for (std::size_t i = 0; i < 0x1000; ++i) {
        mem.write_u8(static_cast<std::size_t>(heap_base + 32 + i), 0xEE);
    }

    const std::uint64_t calloc_ptr = allocator.kcalloc(16, 16);
    CHECK(calloc_ptr != 0, "kcalloc must succeed");
    for (std::size_t i = 0; i < 256; ++i) {
        CHECK(mem.read_u8(static_cast<std::size_t>(calloc_ptr + i)) == 0, "kcalloc memory must be zeroed");
    }

    // Write a recognizable test pattern
    for (std::size_t i = 0; i < 64; ++i) {
        mem.write_u8(static_cast<std::size_t>(calloc_ptr + i), static_cast<std::uint8_t>(0xA0 + i));
    }

    // Reallocate to larger size (512 bytes)
    const std::uint64_t realloc_ptr = allocator.krealloc(calloc_ptr, 512);
    CHECK(realloc_ptr != 0, "krealloc must succeed");
    // Verify first 64 bytes retained
    for (std::size_t i = 0; i < 64; ++i) {
        CHECK(mem.read_u8(static_cast<std::size_t>(realloc_ptr + i)) == static_cast<std::uint8_t>(0xA0 + i),
              "reallocated block must retain existing data");
    }

    // Reallocate to smaller size: should return same pointer
    const std::uint64_t shrink_ptr = allocator.krealloc(realloc_ptr, 32);
    CHECK(shrink_ptr == realloc_ptr, "Shrinking block size should return same pointer");

    CHECK(allocator.kfree(shrink_ptr), "Free reallocated pointer");

    std::cout << "[PASS] test_krealloc_and_kcalloc\n";
    return true;
}

static bool test_corruption_detection_and_boundaries() {
    Memory mem(4 * 1024 * 1024);
    const std::uint64_t heap_base = 0x200000;
    const std::uint64_t heap_size = 0x40000;

    KernelHeapAllocator allocator(mem, heap_base, heap_size);
    CHECK(allocator.initialize(), "Init heap");

    // Invalid pointer frees
    CHECK(!allocator.kfree(0), "kfree(0) must fail");
    CHECK(!allocator.kfree(heap_base - 10), "Out-of-bounds kfree below heap must fail");
    CHECK(!allocator.kfree(heap_base + heap_size + 100), "Out-of-bounds kfree above heap must fail");

    const std::uint64_t ptr = allocator.kmalloc(100);
    CHECK(ptr != 0, "kmalloc must succeed");

    // Free once
    CHECK(allocator.kfree(ptr), "First free must succeed");
    // Double free must fail
    CHECK(!allocator.kfree(ptr), "Double free must fail");

    // Corrupt block magic
    const std::uint64_t hdr_addr = ptr - kHeapHeaderSize;
    mem.write_u64(static_cast<std::size_t>(hdr_addr + 8), 0xDEADDEADBEEFBEEFULL);

    std::string err;
    CHECK(!allocator.validate_heap_integrity(&err), "Heap validation must fail with corrupt magic");
    CHECK(!err.empty(), "Error explanation must be populated");

    // Allocation attempt should detect corruption and abort
    CHECK(allocator.kmalloc(32) == 0, "kmalloc must fail on corrupted heap");

    std::cout << "[PASS] test_corruption_detection_and_boundaries\n";
    return true;
}

static bool test_guest_arm64_bump_allocator() {
    Memory mem(4 * 1024 * 1024);

    const std::uint64_t code_addr = 0x1000;
    const std::uint64_t ctrl_addr = 0x4000;
    const std::uint64_t heap_start = 0x200000;
    const std::uint64_t heap_limit = 0x201000; // 4096 bytes heap

    // Control slot: [0x4000] = current_ptr, [0x4008] = end_ptr
    mem.write_u64(static_cast<std::size_t>(ctrl_addr + 0), heap_start);
    mem.write_u64(static_cast<std::size_t>(ctrl_addr + 8), heap_limit);

    // Emit ARM64 instructions into guest memory
    const auto routine = KernelHeapAllocator::emit_guest_kmalloc_routine();
    for (std::size_t i = 0; i < routine.size(); ++i) {
        mem.write_u32(static_cast<std::size_t>(code_addr + i * 4), routine[i]);
    }

    CPU cpu;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x70000);

    const std::uint64_t halt_addr = 0x9000;

    // Call 1: Allocate 120 bytes
    cpu.set_program_counter(code_addr);
    cpu.write_register(0, ctrl_addr);
    cpu.write_register(1, 120);
    cpu.write_register(30, halt_addr); // Return address (must be 4-byte aligned on ARM64)

    std::size_t steps = 0;
    while (cpu.get_program_counter() != halt_addr && steps < 50) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.read_register(0) == heap_start, "First allocation must start at heap_start (0x200000)");
    const std::uint64_t new_curr1 = mem.read_u64(static_cast<std::size_t>(ctrl_addr));
    // 120 aligned to 16 is 128 (0x80) -> new_curr = 0x200080
    CHECK(new_curr1 == 0x200080, "Heap control pointer must advance by 128 bytes (aligned 120)");

    // Call 2: Allocate 200 bytes
    cpu.set_program_counter(code_addr);
    cpu.write_register(0, ctrl_addr);
    cpu.write_register(1, 200);
    cpu.write_register(30, halt_addr);

    steps = 0;
    while (cpu.get_program_counter() != halt_addr && steps < 50) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.read_register(0) == 0x200080, "Second allocation must start at 0x200080");
    const std::uint64_t new_curr2 = mem.read_u64(static_cast<std::size_t>(ctrl_addr));
    // 200 aligned to 16 is 208 (0xD0) -> new_curr = 0x200080 + 0xD0 = 0x200150
    CHECK(new_curr2 == 0x200150, "Heap control pointer must advance by 208 bytes (aligned 200)");

    // Call 3: Request 10,000 bytes (exceeds heap_limit)
    cpu.set_program_counter(code_addr);
    cpu.write_register(0, ctrl_addr);
    cpu.write_register(1, 10000);
    cpu.write_register(30, halt_addr);

    steps = 0;
    while (cpu.get_program_counter() != halt_addr && steps < 50) {
        cpu.step(mem);
        ++steps;
    }

    CHECK(cpu.read_register(0) == 0, "OOM request must return 0");
    const std::uint64_t new_curr3 = mem.read_u64(static_cast<std::size_t>(ctrl_addr));
    CHECK(new_curr3 == 0x200150, "Failed allocation must not advance heap pointer");

    std::cout << "[PASS] test_guest_arm64_bump_allocator\n";
    return true;
}

int main() {
    std::cout << "Running Stage 194.6 Kernel Heap Allocator Tests..." << std::endl;

    std::cout << "Running test 1..." << std::endl;
    if (!test_heap_initialization_and_alignment()) return 1;
    std::cout << "Running test 2..." << std::endl;
    if (!test_kmalloc_kfree_lifecycle()) return 1;
    std::cout << "Running test 3..." << std::endl;
    if (!test_forward_coalescing()) return 1;
    std::cout << "Running test 4..." << std::endl;
    if (!test_krealloc_and_kcalloc()) return 1;
    std::cout << "Running test 5..." << std::endl;
    if (!test_corruption_detection_and_boundaries()) return 1;
    std::cout << "Running test 6..." << std::endl;
    if (!test_guest_arm64_bump_allocator()) return 1;

    std::cout << "\nAll Stage 194.6 Kernel Heap Allocator tests PASSED successfully." << std::endl;
    return 0;
}
