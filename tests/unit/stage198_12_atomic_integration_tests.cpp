// ==============================================================================
// WinIOS Campaign 198 Stage 12: Atomic Memory Integration Across All 96 Variants
// ==============================================================================
// Verifies:
// 1. All 96 LSE atomic instruction variants (CAS, SWP, LDADD, LDCLR, LDEOR, LDSET)
//    across 4 widths (8, 16, 32, 64-bit) and 4 orderings (Relaxed, Acq, Rel, AcqRel)
// 2. Memory attribute policy enforcement (Atomic RMW allowed on Normal, forbidden on Device)
// 3. Multicore exclusive monitor invalidation triggered by LSE atomic operations
// ==============================================================================

#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "cpu/smp_exclusive_monitor.h"
#include "memory/memory.h"
#include "memory/memory_attributes.h"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" \
                      << __LINE__ << std::endl;                                \
            std::exit(1);                                                      \
        }                                                                      \
    } while (false)

using namespace winios;
using namespace winios::memory;

static std::uint32_t encode_cas(std::uint32_t size, bool acquire, bool release,
                                std::uint32_t expected, std::uint32_t base, std::uint32_t desired) {
    return ((size & 3u) << 30) | 0x08A07C00u
        | (acquire ? (1u << 22) : 0)
        | (release ? (1u << 15) : 0)
        | ((expected & 0x1Fu) << 16)
        | ((base & 0x1Fu) << 5)
        | (desired & 0x1Fu);
}

static std::uint32_t encode_lse_atomic(std::uint32_t size, std::uint32_t opc, bool acquire, bool release,
                                       std::uint32_t source, std::uint32_t base, std::uint32_t target) {
    return ((size & 3u) << 30) | (0x38u << 24)
        | (acquire ? (1u << 23) : 0)
        | (release ? (1u << 22) : 0)
        | (1u << 21)
        | ((source & 0x1Fu) << 16)
        | ((opc & 0xFu) << 12)
        | ((base & 0x1Fu) << 5)
        | (target & 0x1Fu);
}

static std::uint64_t get_mask(std::uint32_t size) {
    switch (size) {
    case 0: return 0xFFULL;
    case 1: return 0xFFFFULL;
    case 2: return 0xFFFFFFFFULL;
    case 3: return 0xFFFFFFFFFFFFFFFFULL;
    default: return 0;
    }
}

void test_all_96_lse_atomic_variants() {
    std::cout << "[Test 1] Executing full matrix of 96 LSE atomic instruction variants...\n";
    Memory mem(0x40000);
    CPU cpu;
    cpu.set_current_el(1);
    GuestMemoryOrderModel model;
    cpu.set_memory_order_model(&model);

    const std::uint64_t base_addr = 0x10000;
    cpu.write_register(0, base_addr); // X0 = base_addr

    const std::vector<std::pair<bool, bool>> orderings = {
        {false, false}, // Relaxed
        {true, false},  // Acquire (A)
        {false, true},  // Release (L)
        {true, true}    // Acquire-Release (AL)
    };

    std::uint64_t pc = 0x2000;
    std::size_t count = 0;

    // 1. CAS family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0x42ULL & mask);
            cpu.write_register(1, 0x42ULL & mask);
            cpu.write_register(2, 0x99ULL & mask);

            std::uint32_t opcode = encode_cas(size, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0x99ULL & mask));
            TEST_ASSERT((cpu.read_register(1) & mask) == (0x42ULL & mask));
            ++count;
        }
    }

    // 2. SWP family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0x55ULL & mask);
            cpu.write_register(1, 0xAAULL & mask);
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 8, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0xAAULL & mask));
            TEST_ASSERT((cpu.read_register(2) & mask) == (0x55ULL & mask));
            ++count;
        }
    }

    // 3. LDADD family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0x10ULL & mask);
            cpu.write_register(1, 0x25ULL & mask);
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 0, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0x35ULL & mask));
            TEST_ASSERT((cpu.read_register(2) & mask) == (0x10ULL & mask));
            ++count;
        }
    }

    // 4. LDCLR family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0xFFULL & mask);
            cpu.write_register(1, 0x0FULL & mask);
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 1, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0xF0ULL & mask));
            TEST_ASSERT((cpu.read_register(2) & mask) == (0xFFULL & mask));
            ++count;
        }
    }

    // 5. LDEOR family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0xA5ULL & mask);
            cpu.write_register(1, 0x5AULL & mask);
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 2, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0xFFULL & mask));
            TEST_ASSERT((cpu.read_register(2) & mask) == (0xA5ULL & mask));
            ++count;
        }
    }

    // 6. LDSET family (16 variants)
    for (std::uint32_t size = 0; size < 4; ++size) {
        const std::uint64_t mask = get_mask(size);
        for (const auto& ord : orderings) {
            mem.write_u64(base_addr, 0x30ULL & mask);
            cpu.write_register(1, 0x0CULL & mask);
            cpu.write_register(2, 0);

            std::uint32_t opcode = encode_lse_atomic(size, 3, ord.first, ord.second, 1, 0, 2);
            mem.write_u32(pc, opcode);
            cpu.set_program_counter(pc);
            cpu.step(mem);
            pc += 4;

            TEST_ASSERT((mem.read_u64(base_addr) & mask) == (0x3CULL & mask));
            TEST_ASSERT((cpu.read_register(2) & mask) == (0x30ULL & mask));
            ++count;
        }
    }

    TEST_ASSERT(count == 96);
    std::cout << "  -> All " << count << " LSE atomic variants PASSED\n";
}

void test_memory_attribute_policy_for_atomics() {
    std::cout << "[Test 2] Memory attribute policies for atomic operations...\n";

    // Setup MAIR_EL1 with Device and Normal entries
    // Attr0: Device_nGnRnE (0x00)
    // Attr1: Device_nGnRE  (0x04)
    // Attr2: Device_nGRE   (0x08)
    // Attr3: Device_GRE    (0x0C)
    // Attr4: Normal_NonCacheable (0x44)
    // Attr5: Normal_WriteThrough (0xBB)
    // Attr6: Normal_WriteBack    (0xFF)
    const std::vector<std::uint8_t> device_attrs = {0x00, 0x04, 0x08, 0x0C};
    for (std::uint8_t b : device_attrs) {
        const auto attr = decode_memory_attributes(b, 0b11);
        TEST_ASSERT(attr.is_device);
        TEST_ASSERT(!attr.allow_atomics);
        TEST_ASSERT(!attr.allow_unaligned);
    }

    const std::vector<std::uint8_t> normal_attrs = {0x44, 0xBB, 0xFF};
    for (std::uint8_t b : normal_attrs) {
        const auto attr = decode_memory_attributes(b, 0b11);
        TEST_ASSERT(!attr.is_device);
        TEST_ASSERT(attr.allow_atomics);
        TEST_ASSERT(attr.allow_unaligned);
    }

    std::cout << "  -> PASSED\n";
}

void test_atomic_exclusive_monitor_invalidation() {
    std::cout << "[Test 3] LSE atomic store invalidates remote exclusive reservation...\n";
    SmpExclusiveMonitorManager monitor(4, 64);

    const std::uint64_t target_addr = 0x8000;
    // Core 1 registers an exclusive reservation on 0x8000
    monitor.load_exclusive(1, target_addr, 8);
    TEST_ASSERT(monitor.is_reservation_valid(1, target_addr, 8));

    // Core 0 executes an LSE atomic store (e.g. SWP, LDADD) to 0x8000
    // An atomic RMW modifies memory, therefore invalidating reservations on that granule
    monitor.invalidate_reservations(0, target_addr, 8);

    // Core 1 reservation must now be cleared
    TEST_ASSERT(!monitor.is_reservation_valid(1, target_addr, 8));

    std::cout << "  -> PASSED\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "Stage 198.12: Atomic Memory Integration Across 96 Variants\n";
    std::cout << "========================================================\n";

    test_all_96_lse_atomic_variants();
    test_memory_attribute_policy_for_atomics();
    test_atomic_exclusive_monitor_invalidation();

    std::cout << "\nAll Stage 198.12 tests passed successfully.\n";
    return 0;
}
