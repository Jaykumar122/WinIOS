#include <iostream>
#include <cstdint>

#include "cpu/mmu.h"
#include "memory/memory.h"
#include "memory/kernel_memory_layout.h"

using namespace mmu;
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

void test_kernel_mapping_wx() {
    std::cout << "[Test 1] Stage 208.12: Kernel Mapping and W^X Enforcement...\n";

    KernelMemoryLayout layout;
    std::string err;

    // 1. Valid Text region (Read + Execute, not Writable)
    KernelMemoryRegion text{};
    text.name = "__TEXT";
    text.kind = MemoryRegionKind::KernelText;
    text.virtual_address = 0xFFFFFF8000000000ULL;
    text.physical_address = 0x20000ULL;
    text.size = 0x10000ULL;
    text.is_readable = true;
    text.is_writable = false;
    text.is_executable = true;
    TEST_CHECK(layout.add_region(text, &err), "Added valid RX KernelText region");

    // 2. Valid Data region (Read + Write, not Executable)
    KernelMemoryRegion data{};
    data.name = "__DATA";
    data.kind = MemoryRegionKind::KernelData;
    data.virtual_address = 0xFFFFFF8000010000ULL;
    data.physical_address = 0x30000ULL;
    data.size = 0x10000ULL;
    data.is_readable = true;
    data.is_writable = true;
    data.is_executable = false;
    TEST_CHECK(layout.add_region(data, &err), "Added valid RW KernelData region");

    // 3. W^X Violation Attempt (Write + Execute simultaneously)
    KernelMemoryRegion bad{};
    bad.name = "ATTACK_RWX";
    bad.kind = MemoryRegionKind::KernelData;
    bad.virtual_address = 0xFFFFFF8000020000ULL;
    bad.physical_address = 0x40000ULL;
    bad.size = 0x4000ULL;
    bad.is_readable = true;
    bad.is_writable = true;
    bad.is_executable = true; // W^X VIOLATION!
    TEST_CHECK(!layout.add_region(bad, &err), "Rejected RWX injection by W^X policy");

    // 4. Test MMU hardware WXN bit (SCTLR_EL1.WXN = bit 19)
    MMU mmu;
    Memory mem(0x200000);
    mmu.set_mair_el1(0x00000000000004FFULL);
    mmu.set_tcr_el1((0b10ULL << 14u) | (28ULL));
    mmu.set_ttbr0_el1(0x30000ULL);

    // Root table entry: RW block (AP=0, low bits 0x701)
    mem.write_u64(0x30000ULL, 0x0000000000000701ULL);

    // Enable MMU with WXN=1 (SCTLR_EL1.WXN = 1 << 19): 0x00C50839 | (1 << 19) = 0x00CD0839
    mmu.set_sctlr_el1(0x00CD0839ULL);
    TEST_CHECK(mmu.is_wxn_enabled(), "SCTLR_EL1.WXN enabled");

    // Attempt to execute from writable block -> Must fail with Permission fault!
    TranslationFault fault{};
    auto trans_ex = mmu.translate(mem, 0x1000ULL, AccessKind::Execute, 1, &fault);
    TEST_CHECK(!trans_ex.has_value(), "Execution on writable memory rejected under WXN");
    TEST_CHECK(fault.type == MmuFaultType::Permission, "WXN enforced via Permission Fault");
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 208.12 Kernel Mapping & W^X Tests ===\n";
    test_kernel_mapping_wx();
    if (g_failures == 0) {
        std::cout << "[PASS] Stage 208.12: All kernel mapping W^X tests passed.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 208.12: " << g_failures << " tests failed.\n";
        return 1;
    }
}
