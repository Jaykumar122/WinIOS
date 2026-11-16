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

void test_kernel_va_map_layout() {
    std::cout << "[Test 1] Stage 209.9: Kernel Virtual Address Map Layout & Gap Protection...\n";

    MMU mmu;
    Memory memory(0x800000); // 8 MiB

    // Configure MMU with TCR 16KB granule, TTBR0 pointing to 0x20000
    const std::uint64_t tcr = (0b10ULL << 14u) | (28ULL); // TG0=16KB, T0SZ=28 (36-bit IAS)
    mmu.set_tcr_el1(tcr);
    mmu.set_ttbr0_el1(0x20000ULL);
    mmu.set_mair_el1(0x00000000000004FFULL); // Attr 0: Normal, Attr 1: Device-nGnRE

    // Populate Level-2 table at 0x20000:
    // Slot 0: 32MB identity map block for DRAM [0x0, 32MB) Normal Cacheable RW (0x701)
    memory.write_u64(0x20000ULL, 0x0000000000000701ULL);
    // Slot 1: Strictly invalid (0x0) - Gap protection
    memory.write_u64(0x20008ULL, 0x0000000000000000ULL);
    // Slot 285: AIC MMIO aperture at 0x23B100000 (0x000000023A000705)
    memory.write_u64(0x20000ULL + (285 * 8), 0x000000023A000705ULL);

    // Enable Stage-1 MMU
    mmu.set_sctlr_el1(0x00C50839ULL);

    // 1. Test translation in mapped DRAM zone (e.g., kernel stack at 0xC000)
    TranslationFault fault{};
    auto t_stack = mmu.translate(memory, 0xC000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(t_stack.has_value() && t_stack.value() == 0xC000ULL, "Stack mapped 1:1");

    // 2. Test translation in AIC2 aperture (0x23B100000)
    bool is_device = false;
    auto t_aic = mmu.translate(memory, 0x23B100000ULL, AccessKind::Read, 1, &fault, &is_device);
    TEST_CHECK(t_aic.has_value() && t_aic.value() == 0x23B100000ULL, "AIC2 aperture mapped 1:1");
    TEST_CHECK(is_device, "AIC2 memory type is Device-nGnRE");

    // 3. Test unmapped gap at Slot 1 (32MB..64MB, e.g., 0x2000000)
    auto t_gap1 = mmu.translate(memory, 0x02000000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!t_gap1.has_value(), "Access to Slot 1 (ghost RAM gap) generates Translation Fault");
    TEST_CHECK(fault.type == MmuFaultType::Translation, "Fault type is Translation Fault");

    // 4. Test unmapped gap at high address (e.g., 0x100000000 = 4GB, Slot 128)
    auto t_gap2 = mmu.translate(memory, 0x100000000ULL, AccessKind::Read, 1, &fault);
    TEST_CHECK(!t_gap2.has_value(), "Access to Slot 128 (4GB gap) generates Translation Fault");
    TEST_CHECK(fault.type == MmuFaultType::Translation, "Fault type is Translation Fault");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 209.9 Kernel VA Map Tests ===\n";
    test_kernel_va_map_layout();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 209.9: All kernel VA map tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 209.9: " << g_failures << " tests failed.\n";
        return 1;
    }
}
