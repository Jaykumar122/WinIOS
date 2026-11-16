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

void test_kernel_mapping_services() {
    std::cout << "[Test 1] Stage 209.10: Kernel Mapping Services (Map, Unmap, Protect, Lookup)...\n";

    MMU mmu;
    Memory memory(0x800000); // 8 MiB

    const std::uint64_t tcr = (0b10ULL << 14u) | (28ULL);
    mmu.set_tcr_el1(tcr);
    mmu.set_ttbr0_el1(0x20000ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    // Initial state: Table at 0x20000 is clean (all zero)
    TranslationFault fault{};

    // 1. Unmapped lookup verification
    auto trans0 = mmu.translate(memory, 0x0ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!trans0.has_value(), "Initially unmapped VA generates Translation Fault");

    // 2. Map Operation: Write Level-2 block descriptor at slot 0 (32MB block)
    // 0x701 = Normal Cacheable, RW, Inner Shareable, AF=1
    memory.write_u64(0x20000ULL, 0x0000000000000701ULL);
    mmu.invalidate_tlb_all();

    auto trans1 = mmu.translate(memory, 0x4000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(trans1.has_value() && trans1.value() == 0x4000ULL, "Mapped address translates successfully");

    // 3. Protect Operation: Change descriptor from RW (0x701) to Read-Only (0x781)
    // In ARMv8, AP[2] = 1 (bit 7) encodes Read-Only
    memory.write_u64(0x20000ULL, 0x0000000000000781ULL);
    mmu.invalidate_tlb_all();

    // Read succeeds
    auto trans_ro_read = mmu.translate(memory, 0x4000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(trans_ro_read.has_value(), "Read on Read-Only mapping succeeds");

    // Write generates Permission Fault
    auto trans_ro_write = mmu.translate(memory, 0x4000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!trans_ro_write.has_value(), "Write on Read-Only mapping is blocked");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Trapped with Permission Fault");

    // 4. Unmap Operation: Clear descriptor to 0x0
    memory.write_u64(0x20000ULL, 0x0ULL);
    mmu.invalidate_tlb_all();

    auto trans_unmapped = mmu.translate(memory, 0x4000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!trans_unmapped.has_value(), "Unmapped address returns Translation Fault");
    TEST_CHECK(fault.type == MmuFaultType::Translation, "Fault type is Translation Fault");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.10 Kernel Mapping Services Tests ===\n";
    test_kernel_mapping_services();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.10: All kernel mapping services tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.10: " << g_failures << " tests failed.\n";
        return 1;
    }
}
