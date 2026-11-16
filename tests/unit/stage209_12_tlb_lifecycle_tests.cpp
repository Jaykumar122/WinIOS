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

void test_tlb_lifecycle() {
    std::cout << "[Test 1] Stage 209.12: TLB Invalidation Lifecycle & Stale Translation Prevention...\n";

    MMU mmu;
    Memory memory(0x800000);

    const std::uint64_t tcr = (0b10ULL << 14u) | (28ULL);
    mmu.set_tcr_el1(tcr);
    mmu.set_ttbr0_el1(0x20000ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};

    // 1. Initial mapping: 0x20000 slot 0 has RW descriptor 0x701
    memory.write_u64(0x20000ULL, 0x0000000000000701ULL);
    mmu.invalidate_tlb_all();

    auto t1 = mmu.translate(memory, 0x10000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(t1.has_value(), "Initial translation cached in TLB");

    // 2. Change descriptor in guest RAM to Read-Only (0x781)
    memory.write_u64(0x20000ULL, 0x0000000000000781ULL);

    // 3. Without TLB invalidation, translation may use cached entry
    // Now perform TLBI: flush TLB (representing TLBI VMALLE1IS)
    mmu.invalidate_tlb_all();

    // 4. Translate again: must see updated Read-Only permissions!
    auto t2 = mmu.translate(memory, 0x10000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!t2.has_value(), "Stale translation invalidated: Write is now blocked");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Trapped with Permission Fault after TLBI");

    // 5. Invalidate single VA
    mmu.invalidate_tlb_vaa(0x10000ULL);
    auto t3 = mmu.translate(memory, 0x10000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(t3.has_value(), "Read succeeds on freshly populated TLB entry");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.12 TLB Lifecycle Tests ===\n";
    test_tlb_lifecycle();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.12: All TLB lifecycle tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.12: " << g_failures << " tests failed.\n";
        return 1;
    }
}
