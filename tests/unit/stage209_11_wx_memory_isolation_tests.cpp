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

void test_wx_memory_isolation() {
    std::cout << "[Test 1] Stage 209.11: W^X Enforcement & Memory Isolation Tests...\n";

    MMU mmu;
    Memory memory(0x800000);

    const std::uint64_t tcr = (0b10ULL << 14u) | (28ULL);
    mmu.set_tcr_el1(tcr);
    mmu.set_ttbr0_el1(0x20000ULL);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_sctlr_el1(0x00C50839ULL);

    TranslationFault fault{};

    // 1. Configure Executable Code Block: Read-Only, Executable (PXN=0, UXN=0, AP=0b10 -> 0x781)
    // Bit 53 (PXN) = 0, Bit 54 (UXN) = 0, AP[2] = 1 (RO)
    const std::uint64_t rx_desc = 0x0000000000000781ULL;
    memory.write_u64(0x20000ULL, rx_desc);
    mmu.invalidate_tlb_all();

    // Instruction Fetch on RX page: SUCCEEDS
    auto t_fetch = mmu.translate(memory, 0x1000ULL, AccessKind::Execute, 1, &fault);
    TEST_CHECK(t_fetch.has_value(), "Instruction fetch on RX block succeeds");

    // Write on RX page: FAILS with Permission Fault
    auto t_write_rx = mmu.translate(memory, 0x1000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(!t_write_rx.has_value(), "Write on RX block blocked");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault type is Permission Fault");

    // 2. Configure Writable Data Block: Read-Write, Non-Executable (PXN=1, UXN=1, AP=0b00)
    // Bit 53 (PXN=1) = 1ULL << 53, Bit 54 (UXN=1) = 1ULL << 54, AP[2] = 0 (RW)
    const std::uint64_t rw_nx_desc = 0x0000000000000701ULL | (1ULL << 53u) | (1ULL << 54u);
    memory.write_u64(0x20000ULL, rw_nx_desc);
    mmu.invalidate_tlb_all();

    // Write on RW_NX page: SUCCEEDS
    auto t_write_rw = mmu.translate(memory, 0x1000ULL, AccessKind::Write, 1, &fault);
    TEST_CHECK(t_write_rw.has_value(), "Write on RW_NX block succeeds");

    // Instruction Fetch on RW_NX page: FAILS with Permission Fault
    auto t_fetch_rw = mmu.translate(memory, 0x1000ULL, AccessKind::Execute, 1, &fault);
    TEST_CHECK(!t_fetch_rw.has_value(), "Instruction fetch on RW_NX block blocked");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "Fault type is Permission Fault");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.11 W^X Memory Isolation Tests ===\n";
    test_wx_memory_isolation();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.11: All W^X memory isolation tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.11: " << g_failures << " tests failed.\n";
        return 1;
    }
}
