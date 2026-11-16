#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "memory/kernel_memory_layout.h"

using namespace winios::memory;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_wx_protection_and_segment_isolation() {
    std::cout << "[Test 1] Kernel Segment W^X Protection & Isolation...\n";

    KernelMemoryLayout layout(0x800000000ULL, 0x180000000ULL);
    layout.clear_regions();

    // Canonical Darwin Kernel Segment Layout
    KernelMemoryRegion text_reg{"__TEXT", MemoryRegionKind::KernelText, 0xFFFFFE0007004000ULL, 0x800000ULL, 0x800000000ULL, true, false, true, false};
    KernelMemoryRegion const_reg{"__DATA_CONST", MemoryRegionKind::ReadOnlyData, 0xFFFFFE0007804000ULL, 0x200000ULL, 0x800800000ULL, true, false, false, false};
    KernelMemoryRegion data_reg{"__DATA", MemoryRegionKind::WritableData, 0xFFFFFE0007A04000ULL, 0x400000ULL, 0x800A00000ULL, true, true, false, false};
    KernelMemoryRegion bss_reg{"__BSS", MemoryRegionKind::ZeroInitData, 0xFFFFFE0007E04000ULL, 0x200000ULL, 0x800E00000ULL, true, true, false, false};

    layout.add_region(text_reg);
    layout.add_region(const_reg);
    layout.add_region(data_reg);
    layout.add_region(bss_reg);

    std::string err;
    bool wx_valid = layout.check_wx_policy(&err);
    TEST_CHECK(wx_valid, "Canonical Darwin layout must strictly pass W^X policy");
    TEST_CHECK(err.empty(), "Zero W^X violations reported");

    // Inject malicious W|X region
    KernelMemoryRegion mal_reg{"__MALICIOUS", MemoryRegionKind::WritableData, 0xFFFFFE0008004000ULL, 0x40000ULL, 0x801000000ULL, true, true, true, false};
    std::string add_err;
    bool add_ok = layout.add_region(mal_reg, &add_err);
    TEST_CHECK(!add_ok, "Layout strictly rejects addition of RWX region");
    TEST_CHECK(!add_err.empty(), "Violations reported for RWX segment: " + add_err);

    std::cout << "  -> W^X segment protection verified.\n";
}

void test_permission_transitions() {
    std::cout << "[Test 2] Segment Relocation to Runtime Protection Transitions...\n";

    KernelMemoryLayout layout(0x800000000ULL, 0x180000000ULL);
    layout.clear_regions();

    // Staging phase: __TEXT initially mapped RW for early fixups/relocations
    KernelMemoryRegion text_staging{"__TEXT", MemoryRegionKind::KernelText, 0xFFFFFE0007004000ULL, 0x800000ULL, 0x800000000ULL, true, true, false, false};
    layout.add_region(text_staging);

    const auto* reg = layout.find_region(0xFFFFFE0007004000ULL);
    TEST_CHECK(reg != nullptr, "Found staging __TEXT region");
    TEST_CHECK(reg->is_writable, "Staging text is writable");
    TEST_CHECK(!reg->is_executable, "Staging text is not executable during fixups");

    // Re-add as locked RX for runtime
    layout.clear_regions();
    KernelMemoryRegion text_runtime{"__TEXT", MemoryRegionKind::KernelText, 0xFFFFFE0007004000ULL, 0x800000ULL, 0x800000000ULL, true, false, true, false};
    layout.add_region(text_runtime);

    const auto* reg_rt = layout.find_region(0xFFFFFE0007004000ULL);
    TEST_CHECK(reg_rt != nullptr, "Found runtime __TEXT region");
    TEST_CHECK(!reg_rt->is_writable, "Runtime text is write-protected");
    TEST_CHECK(reg_rt->is_executable, "Runtime text is executable");

    std::string err;
    TEST_CHECK(layout.check_wx_policy(&err), "Final layout satisfies W^X");

    std::cout << "  -> Protection transitions verified successfully.\n";
}

} // namespace

int main() {
    std::cout << "=== Running Stage 206.14 Kernel Memory Protection Tests ===\n";
    test_wx_protection_and_segment_isolation();
    test_permission_transitions();

    if (g_failures == 0) {
        std::cout << "=== All Stage 206.14 Tests PASSED ===\n";
        return 0;
    } else {
        std::cerr << "=== " << g_failures << " Stage 206.14 Tests FAILED ===\n";
        return 1;
    }
}
