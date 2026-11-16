#include <iostream>
#include <cstdint>

#include "cpu/mmu.h"
#include "memory/memory.h"

using namespace mmu;

namespace {

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_guest_page_table_translation_walks() {
    std::cout << "[Test 1] Stage 208.9: Guest-Created Page Table Walks Driving Translation...\n";

    Memory mem(0x800000); // 8 MB guest DRAM
    MMU mmu;

    // Set up MAIR_EL1: Attr0 = Normal WB (0xFF), Attr1 = Device-nGnRE (0x04)
    mmu.set_mair_el1(0x00000000000004FFULL);

    // Set up TCR_EL1: TG0=16KB (0b10 << 14), TG1=16KB (0b01 << 30), T0SZ=28 (36-bit VA)
    const std::uint64_t tcr = (0b10ULL << 14u) | (0b01ULL << 30u) | (28ULL);
    mmu.set_tcr_el1(tcr);

    // Set TTBR0_EL1 to point to guest-created root table at 0x30000
    const std::uint64_t root_pa = 0x30000ULL;
    mmu.set_ttbr0_el1(root_pa);

    // Write guest-created Level 2 descriptors into memory at root_pa
    // Entry 0 (VA [0x0, 32MB)): 32MB identity block descriptor (Normal Memory, RW, Inner Shareable, AF=1)
    // Descr: PA=0x0, AF=1 (bit 10), SH=3 (bits 9:8), AP=0 (RW, bits 7:6), Attr=0 (bits 4:2), Type=1 (Block)
    mem.write_u64(static_cast<std::size_t>(root_pa), 0x0000000000000701ULL);

    // Entry 1 (VA [32MB, 64MB)): Read-Only block descriptor
    // Descr: PA=0x2000000 (32MB), AF=1, SH=3, AP=2 (RO, bits 7:6 = 0b10), Attr=0, Type=1 (Block)
    // 0x0000000002000781ULL -> AP=2 is bit 7 (0x80)
    mem.write_u64(static_cast<std::size_t>(root_pa + 8), 0x0000000002000781ULL);

    // Enable Stage-1 MMU: SCTLR_EL1.M = 1
    mmu.set_sctlr_el1(0x00C50839ULL);
    TEST_CHECK(mmu.is_stage1_enabled(), "Stage-1 MMU enabled");

    // 1. Test translation of VA in Entry 0 (e.g. 0x1BA0)
    TranslationFault fault{};
    bool is_device = false;
    auto trans_entry0 = mmu.translate(mem, 0x1BA0ULL, AccessKind::Read, 1, &fault, &is_device);
    TEST_CHECK(trans_entry0.has_value(), "Translated VA 0x1BA0 using guest Level 2 descriptor");
    TEST_CHECK(trans_entry0.value() == 0x1BA0ULL, "Physical address matches identity mapping");
    TEST_CHECK(!is_device, "Memory attribute is Normal memory");

    // 2. Test Write access to Entry 0 (Allowed)
    auto trans_write0 = mmu.translate(mem, 0x50000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(trans_write0.has_value(), "Write to RW block allowed");

    // 3. Test Read access to Entry 1 (VA 0x2000100 -> PA 0x2000100)
    auto trans_ro_read = mmu.translate(mem, 0x2000100ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(trans_ro_read.has_value(), "Read from RO block allowed");
    TEST_CHECK(trans_ro_read.value() == 0x2000100ULL, "PA matches 0x2000100");

    // 4. Test Write access to Entry 1 (Must fail with Permission Fault!)
    auto trans_ro_write = mmu.translate(mem, 0x2000100ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!trans_ro_write.has_value(), "Write to RO block rejected by MMU");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Trapped with Permission Fault");

    // 5. Test access to unmapped VA (Entry 10 at 320MB, not populated in page table)
    auto trans_unmapped = mmu.translate(mem, 0x14000000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!trans_unmapped.has_value(), "Access to unmapped VA rejected");
    TEST_CHECK(fault.type == MmuFaultType::Translation, "Trapped with Translation Fault");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.9 Real Guest Page Table Walk Tests ===\n";
    test_guest_page_table_translation_walks();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.9: All real page table walk tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.9: " << g_failures << " tests failed.\n";
        return 1;
    }
}
