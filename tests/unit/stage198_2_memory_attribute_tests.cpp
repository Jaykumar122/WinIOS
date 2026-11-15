// =============================================================================
// WinIOS Campaign 198 Stage 198.2: Memory Attribute and Translation Tests
// =============================================================================

#include "memory/memory_attributes.h"
#include "memory/unified_memory.h"
#include "cpu/mmu.h"

#include <cassert>
#include <iostream>
#include <string>

#define TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion failed: " #expr " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::abort(); \
        } \
    } while (0)

using namespace winios;
using namespace winios::memory;

namespace {

void test_mair_decoding_device_types() {
    std::cout << "[Test 1] MAIR Device memory type decoding...\n";

    // 0x00 -> Device-nGnRnE
    auto d1 = decode_memory_attributes(0x00, 0b10);
    TEST_ASSERT(d1.is_device);
    TEST_ASSERT(d1.type == MemoryType::Device_nGnRnE);
    TEST_ASSERT(!d1.allow_unaligned);
    TEST_ASSERT(!d1.allow_atomics);
    TEST_ASSERT(!d1.allow_dc_zva);
    TEST_ASSERT(d1.inner_cacheability == Cacheability::NonCacheable);
    TEST_ASSERT(d1.outer_cacheability == Cacheability::NonCacheable);

    // 0x04 -> Device-nGnRE
    auto d2 = decode_memory_attributes(0x04, 0b10);
    TEST_ASSERT(d2.is_device);
    TEST_ASSERT(d2.type == MemoryType::Device_nGnRE);
    TEST_ASSERT(!d2.allow_unaligned);
    TEST_ASSERT(!d2.allow_atomics);
    TEST_ASSERT(!d2.allow_dc_zva);

    // 0x08 -> Device-nGRE
    auto d3 = decode_memory_attributes(0x08, 0b10);
    TEST_ASSERT(d3.is_device);
    TEST_ASSERT(d3.type == MemoryType::Device_nGRE);

    // 0x0C -> Device-GRE
    auto d4 = decode_memory_attributes(0x0C, 0b10);
    TEST_ASSERT(d4.is_device);
    TEST_ASSERT(d4.type == MemoryType::Device_GRE);

    std::cout << "  -> PASSED\n";
}

void test_mair_decoding_normal_memory() {
    std::cout << "[Test 2] MAIR Normal memory type and shareability decoding...\n";

    // 0xFF -> Normal Write-Back RW-Allocate (Inner=WB, Outer=WB)
    auto n1 = decode_memory_attributes(0xFF, 0b11);
    TEST_ASSERT(!n1.is_device);
    TEST_ASSERT(n1.type == MemoryType::Normal);
    TEST_ASSERT(n1.allow_unaligned);
    TEST_ASSERT(n1.allow_atomics);
    TEST_ASSERT(n1.allow_dc_zva);
    TEST_ASSERT(n1.inner_cacheability == Cacheability::WriteBack);
    TEST_ASSERT(n1.outer_cacheability == Cacheability::WriteBack);
    TEST_ASSERT(n1.shareability == ShareabilityDomain::InnerShareable);

    // 0x44 -> Normal Non-cacheable
    auto n2 = decode_memory_attributes(0x44, 0b10);
    TEST_ASSERT(!n2.is_device);
    TEST_ASSERT(n2.inner_cacheability == Cacheability::NonCacheable);
    TEST_ASSERT(n2.outer_cacheability == Cacheability::NonCacheable);
    TEST_ASSERT(n2.shareability == ShareabilityDomain::OuterShareable);

    // 0xBB -> Normal Write-Through
    auto n3 = decode_memory_attributes(0xBB, 0b00);
    TEST_ASSERT(!n3.is_device);
    TEST_ASSERT(n3.inner_cacheability == Cacheability::WriteThrough);
    TEST_ASSERT(n3.outer_cacheability == Cacheability::WriteThrough);
    TEST_ASSERT(n3.shareability == ShareabilityDomain::NonShareable);

    std::cout << "  -> PASSED\n";
}

void test_mmu_translation_with_multiple_mair_attributes() {
    std::cout << "[Test 3] MMU translation with multiple MAIR attributes...\n";
    Memory mem(1024 * 1024);
    mmu::MMU mmu_inst;

    // MAIR_EL1:
    // Attr0: 0xFF (Normal WB)
    // Attr1: 0x00 (Device-nGnRnE)
    // Attr2: 0x04 (Device-nGnRE)
    // Attr3: 0x44 (Normal NC)
    const std::uint64_t mair = (0x44ULL << 24u) | (0x04ULL << 16u) | (0x00ULL << 8u) | 0xFFULL;
    mmu_inst.set_mair_el1(mair);

    // TCR_EL1: TG0=16KB, T0SZ=28
    mmu_inst.set_tcr_el1((2ULL << 14u) | 28ULL);
    const std::uint64_t ttbr_base = 0x10000;
    mmu_inst.set_ttbr0_el1(ttbr_base);
    mmu_inst.set_sctlr_el1(0x5ULL); // MMU on

    // L2 table at ttbr_base points to L3 table at ttbr_base + 0x4000
    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);

    // Map 4 pages:
    // Page 0 (0x0000): AttrIndx=0 (Normal WB) -> PA 0x40000
    mem.write_u64(l3_base + 0, 0x40000ULL | 0x3ULL | (1ULL << 10u) | (0ULL << 2u));
    // Page 1 (0x4000): AttrIndx=1 (Device-nGnRnE) -> PA 0x44000
    mem.write_u64(l3_base + 8, 0x44000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 2u));
    // Page 2 (0x8000): AttrIndx=2 (Device-nGnRE) -> PA 0x48000
    mem.write_u64(l3_base + 16, 0x48000ULL | 0x3ULL | (1ULL << 10u) | (2ULL << 2u));
    // Page 3 (0xC000): AttrIndx=3 (Normal NC) -> PA 0x4C000
    mem.write_u64(l3_base + 24, 0x4C000ULL | 0x3ULL | (1ULL << 10u) | (3ULL << 2u));

    // Verify translations
    mmu::TranslationFault fault;
    bool is_dev = false;

    // Page 0: Normal
    auto r0 = mmu_inst.translate(mem, 0x0000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r0.has_value() && r0.value() == 0x40000ULL);
    TEST_ASSERT(!is_dev);

    // Page 1: Device-nGnRnE
    auto r1 = mmu_inst.translate(mem, 0x4000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r1.has_value() && r1.value() == 0x44000ULL);
    TEST_ASSERT(is_dev);

    // Page 2: Device-nGnRE
    auto r2 = mmu_inst.translate(mem, 0x8000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r2.has_value() && r2.value() == 0x48000ULL);
    TEST_ASSERT(is_dev);

    // Page 3: Normal NC
    auto r3 = mmu_inst.translate(mem, 0xC000, mmu::AccessKind::Read, 1, &fault, &is_dev);
    TEST_ASSERT(r3.has_value() && r3.value() == 0x4C000ULL);
    TEST_ASSERT(!is_dev);

    std::cout << "  -> PASSED\n";
}

void test_device_memory_atomics_and_dc_zva_rejection() {
    std::cout << "[Test 4] Device memory atomic and DC ZVA rejection...\n";
    Memory mem(1024 * 1024);
    mmu::MMU mmu_inst;

    const std::uint64_t mair = (0x04ULL << 16u) | (0x00ULL << 8u) | 0xFFULL;
    mmu_inst.set_mair_el1(mair);
    mmu_inst.set_tcr_el1((2ULL << 14u) | 28ULL);
    const std::uint64_t ttbr_base = 0x10000;
    mmu_inst.set_ttbr0_el1(ttbr_base);
    mmu_inst.set_sctlr_el1(0x5ULL); // MMU on

    const std::uint64_t l3_base = ttbr_base + 0x4000;
    mem.write_u64(ttbr_base, l3_base | 0x3ULL);
    mem.write_u64(l3_base + 8, 0x44000ULL | 0x3ULL | (1ULL << 10u) | (1ULL << 2u)); // Page 1: Device-nGnRnE
    mem.write_u64(l3_base + 16, 0x48000ULL | 0x3ULL | (1ULL << 10u) | (2ULL << 2u)); // Page 2: Device-nGnRE

    UnifiedMemoryView view(mem, &mmu_inst, 1, true);

    // DC ZVA to Device-nGnRnE (VA 0x4000)
    bool caught_zva1 = false;
    try {
        view.cache_zero_block(0x4000, 64);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_zva1 = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u); // Alignment fault FSC 0x21
    }
    TEST_ASSERT(caught_zva1);

    // DC ZVA to Device-nGnRE (VA 0x8000)
    bool caught_zva2 = false;
    try {
        view.cache_zero_block(0x8000, 64);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_zva2 = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u);
    }
    TEST_ASSERT(caught_zva2);

    // Unaligned access to Device-nGnRE
    bool caught_unaligned = false;
    try {
        (void)view.read_u32(0x8002);
    } catch (const SynchronousExceptionSignal& sig) {
        caught_unaligned = true;
        TEST_ASSERT((sig.exception.iss & 0x3Fu) == 0x21u);
    }
    TEST_ASSERT(caught_unaligned);

    std::cout << "  -> PASSED\n";
}

} // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << "Stage 198.2: Memory Attribute and Translation Tests\n";
    std::cout << "====================================================\n";

    test_mair_decoding_device_types();
    test_mair_decoding_normal_memory();
    test_mmu_translation_with_multiple_mair_attributes();
    test_device_memory_atomics_and_dc_zva_rejection();

    std::cout << "\nAll Stage 198.2 Memory Attribute tests passed successfully!\n";
    return 0;
}
