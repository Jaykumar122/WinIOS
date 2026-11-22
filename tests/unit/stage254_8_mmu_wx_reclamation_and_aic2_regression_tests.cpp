// ============================================================================
// Stage 254.5 - 254.8: MMU Descriptor Cross-Validation, W^X Alias Reclamation,
// Kernel/DT/BootArgs Ownership & Six-Core AIC2 Exception Regression
// ============================================================================

#include "cpu/arm64_descriptor_decoder.h"
#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "loader/kernel_interval_map.h"
#include "memory/memory.h"
#include "memory/xnu_page_allocator.h"
#include "memory/xnu_vm_map.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 254.5: Kernel Image, DT, and BootArgs Ownership Cross-Validation
// ----------------------------------------------------------------------------
static void test_stage254_5_kernel_dt_bootargs_ownership() {
    std::cout << "[Stage 254.5] Testing Kernel Image, DT, and BootArgs Ownership Cross-Validation...\n";

    KernelPhysicalIntervalMap pmap(0x0ULL, 0x800000ULL);
    std::string err;

    // Register all authoritative boot segments
    TEST_ASSERT(pmap.add_interval({"KernelText", IntervalType::KernelText, 0x00000000ULL, 0x00004000ULL, 0x5, 0x5, true, "Mach-O __TEXT"}, &err), "Add __TEXT");
    TEST_ASSERT(pmap.add_interval({"KernelData", IntervalType::KernelData, 0x00010000ULL, 0x00020000ULL, 0x3, 0x3, true, "128KB __DATA"}, &err), "Add __DATA");
    TEST_ASSERT(pmap.add_interval({"DeviceTree", IntervalType::DeviceTree, 0x00030000ULL, 0x00004000ULL, 0x3, 0x3, true, "DeviceTree"}, &err), "Add DT");
    TEST_ASSERT(pmap.add_interval({"BootArgs", IntervalType::BootArgs, 0x00034000ULL, 0x00004000ULL, 0x3, 0x3, true, "BootArgs"}, &err), "Add BootArgs");
    TEST_ASSERT(pmap.add_interval({"BootstrapStack", IntervalType::BootstrapStack, 0x00038000ULL, 0x00010000ULL, 0x3, 0x3, true, "Stack"}, &err), "Add Stack");
    TEST_ASSERT(pmap.add_interval({"PageTables", IntervalType::PageTables, 0x00048000ULL, 0x00008000ULL, 0x3, 0x3, true, "PageTables"}, &err), "Add PageTables");
    TEST_ASSERT(pmap.add_interval({"DynamicPool", IntervalType::DynamicPool, 0x00050000ULL, 0x007B0000ULL, 0x3, 0x3, false, "DynamicPool"}, &err), "Add DynamicPool");

    // Cross-validate architectural register contracts:
    // x0 must point to boot_args (0x00034000 in Model A)
    // x1 must point to device tree blob (0x00030000 in Model A)
    // SP_EL1 must point to stack top (0x00048000 in Model A)
    const uint64_t expected_x0 = 0x00034000ULL;
    const uint64_t expected_x1 = 0x00030000ULL;
    const uint64_t expected_sp = 0x00048000ULL;

    const auto* ba_entry = pmap.find_interval_by_name("BootArgs");
    const auto* dt_entry = pmap.find_interval_by_name("DeviceTree");
    const auto* sp_entry = pmap.find_interval_by_name("BootstrapStack");

    TEST_ASSERT(ba_entry && ba_entry->base == expected_x0, "x0 matches BootArgs base address");
    TEST_ASSERT(dt_entry && dt_entry->base == expected_x1, "x1 matches DeviceTree base address");
    TEST_ASSERT(sp_entry && sp_entry->end() == expected_sp, "SP_EL1 matches BootstrapStack top address");

    // Mutual exclusion: Verify no pair of boot regions overlap
    const auto& intervals = pmap.intervals();
    for (std::size_t i = 0; i < intervals.size(); ++i) {
        for (std::size_t j = i + 1; j < intervals.size(); ++j) {
            TEST_ASSERT(!intervals[i].overlaps_with(intervals[j].base, intervals[j].size),
                        "Boot segments must have zero pairwise overlap");
        }
    }

    // Verify loader segment protections:
    const auto* text_entry = pmap.find_interval_by_name("KernelText");
    const auto* data_entry = pmap.find_interval_by_name("KernelData");
    TEST_ASSERT(text_entry && text_entry->prot == 0x5, "KernelText is Read+Execute (0x5)");
    TEST_ASSERT(data_entry && data_entry->prot == 0x3, "KernelData is Read+Write (0x3)");
    TEST_ASSERT(!text_entry->is_wx_violation(), "KernelText does not violate W^X");
    TEST_ASSERT(!data_entry->is_wx_violation(), "KernelData does not violate W^X");

    std::cout << "[Stage 254.5] Kernel image, DT, and BootArgs cross-validation complete.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 254.6: Guest MMU Descriptor Cross-Validation
// ----------------------------------------------------------------------------
static void test_stage254_6_mmu_ownership_cross_validation() {
    std::cout << "[Stage 254.6] Testing Guest MMU Descriptor Cross-Validation...\n";

    // Build synthetic ARM64 Stage 1 Page Descriptors for 16 KiB granule:
    // Bits [1:0] = 0b11 (Page descriptor)
    // Bits [4:2] = AttrIndx: 0 = Normal DRAM (Write-back cacheable), 1 = Device-nGnRE
    // Bits [7:6] = AP[2:1]: 0b00 = Read-Write (EL1 only), 0b10 = Read-Only (EL1 only)
    // Bit  [10]  = Access Flag (AF = 1)
    // Bit  [53]  = PXN (Privileged Execute-Never)
    // Bit  [54]  = UXN (Unprivileged Execute-Never)

    // 1. Text Page Descriptor: Normal DRAM, Read-Only EL1, Executable (PXN=0, UXN=1)
    // Address = 0x00000000
    // AP = 0b10 (RO), AttrIndx = 0 (Normal DRAM), AF = 1, PXN = 0, UXN = 1
    const uint64_t text_desc = (0x00000000ULL & 0xFFFFC000ULL)
                             | (1ULL << 54)        // UXN = 1
                             | (0ULL << 53)        // PXN = 0 (Executable at EL1)
                             | (1ULL << 10)        // AF = 1
                             | (0b10ULL << 6)      // AP[2:1] = 0b10 (Read-Only)
                             | (0ULL << 2)         // AttrIndx = 0 (Normal DRAM)
                             | 0b11ULL;            // Page descriptor

    auto decoded_text = Arm64DescriptorDecoder::decode(text_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(decoded_text.valid, "Text descriptor is valid");
    TEST_ASSERT(decoded_text.type == DecodedDescriptorType::Page, "Text is Page descriptor");
    TEST_ASSERT(decoded_text.attr_indx == 0, "Text is Normal DRAM (AttrIndx 0)");
    TEST_ASSERT(!decoded_text.is_writable(false), "Text is NOT writable at EL1");
    TEST_ASSERT(decoded_text.is_executable(false), "Text IS executable at EL1");
    TEST_ASSERT(!decoded_text.is_wx_violation(false), "Text strictly satisfies W^X");

    // 2. Data Page Descriptor: Normal DRAM, Read-Write EL1, Execute-Never (PXN=1, UXN=1)
    // Address = 0x00010000
    const uint64_t data_desc = (0x00010000ULL & 0xFFFFC000ULL)
                             | (1ULL << 54)        // UXN = 1
                             | (1ULL << 53)        // PXN = 1 (Never executable)
                             | (1ULL << 10)        // AF = 1
                             | (0b00ULL << 6)      // AP[2:1] = 0b00 (Read-Write EL1)
                             | (0ULL << 2)         // AttrIndx = 0 (Normal DRAM)
                             | 0b11ULL;            // Page descriptor

    auto decoded_data = Arm64DescriptorDecoder::decode(data_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(decoded_data.valid, "Data descriptor is valid");
    TEST_ASSERT(decoded_data.attr_indx == 0, "Data is Normal DRAM (AttrIndx 0)");
    TEST_ASSERT(decoded_data.is_writable(false), "Data IS writable at EL1");
    TEST_ASSERT(!decoded_data.is_executable(false), "Data is NOT executable at EL1");
    TEST_ASSERT(!decoded_data.is_wx_violation(false), "Data strictly satisfies W^X");

    // 3. MMIO Device-nGnRE Descriptor (e.g. AIC2 at 0x23B100000)
    // AttrIndx = 1 (Device-nGnRE), RW, XN (PXN=1, UXN=1)
    const uint64_t mmio_desc = (0x23B100000ULL & 0xFFFFC000ULL)
                             | (1ULL << 54)        // UXN = 1
                             | (1ULL << 53)        // PXN = 1
                             | (1ULL << 10)        // AF = 1
                             | (0b00ULL << 6)      // AP[2:1] = 0b00 (RW)
                             | (1ULL << 2)         // AttrIndx = 1 (Device-nGnRE)
                             | 0b11ULL;

    auto decoded_mmio = Arm64DescriptorDecoder::decode(mmio_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(decoded_mmio.valid, "MMIO descriptor is valid");
    TEST_ASSERT(decoded_mmio.attr_indx == 1, "MMIO is Device-nGnRE (AttrIndx 1)");
    TEST_ASSERT(decoded_mmio.is_writable(false), "MMIO is writable");
    TEST_ASSERT(!decoded_mmio.is_executable(false), "MMIO is strictly non-executable");
    TEST_ASSERT(!decoded_mmio.is_wx_violation(false), "MMIO strictly satisfies W^X");

    // 4. Illegal W+X Descriptor Check: Writable and Executable must be flagged as violation
    const uint64_t illegal_desc = (0x00050000ULL & 0xFFFFC000ULL)
                                | (0ULL << 54)        // UXN = 0
                                | (0ULL << 53)        // PXN = 0 (Executable)
                                | (1ULL << 10)        // AF = 1
                                | (0b00ULL << 6)      // AP = RW
                                | (0ULL << 2)
                                | 0b11ULL;

    auto decoded_illegal = Arm64DescriptorDecoder::decode(illegal_desc, 3, Arm64Granule::Granule16KB);
    TEST_ASSERT(decoded_illegal.valid, "Descriptor is syntactically valid");
    TEST_ASSERT(decoded_illegal.is_writable(false), "Descriptor is writable");
    TEST_ASSERT(decoded_illegal.is_executable(false), "Descriptor is executable");
    TEST_ASSERT(decoded_illegal.is_wx_violation(false), "Illegal W+X descriptor MUST flag W^X violation");

    std::cout << "[Stage 254.6] MMU descriptor cross-validation complete.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 254.7: W^X Alias Tracking & Reclamation Hardening
// ----------------------------------------------------------------------------
static void test_stage254_7_wx_alias_reclamation_hardening() {
    std::cout << "[Stage 254.7] Testing W^X Alias Tracking & Reclamation Hardening...\n";

    XnuVmMap vm_map(0x0ULL, 0x800000ULL, "kernel_map");
    vm_map.set_strict_wx(true);
    TEST_ASSERT(vm_map.is_strict_wx(), "Strict W^X is enabled");

    std::string err;

    // Register a physical frame at PA 0x00050000 as Read+Write via VA 0x00050000
    TEST_ASSERT(vm_map.register_physical_alias(0x00050000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err),
                "Register initial RW alias to PA 0x50000");
    TEST_ASSERT(vm_map.alias_count() == 1, "Alias count is 1");

    // Attempting to create an execute (RX) alias to the same physical frame PA 0x00050000 via VA 0x00090000 must FAIL W^X
    TEST_ASSERT(!vm_map.check_alias_wx(0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "W^X check must reject RX alias for existing RW physical frame");
    TEST_ASSERT(!vm_map.register_physical_alias(0x00090000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "Registering RX alias to existing RW physical frame must be rejected");

    // Register a compatible RW alias to PA 0x00050000 via VA 0x00060000: succeeds
    TEST_ASSERT(vm_map.register_physical_alias(0x00060000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err),
                "Register compatible second RW alias to same PA succeeds");
    TEST_ASSERT(vm_map.alias_count() == 2, "Alias count is 2");

    // Unregister VA 0x00050000:
    TEST_ASSERT(vm_map.unregister_physical_alias(0x00050000ULL, 0x4000ULL, &err), "Unregister first alias");
    TEST_ASSERT(vm_map.alias_count() == 1, "Alias count is 1");

    // Unregister VA 0x00060000:
    TEST_ASSERT(vm_map.unregister_physical_alias(0x00060000ULL, 0x4000ULL, &err), "Unregister second alias");
    TEST_ASSERT(vm_map.alias_count() == 0, "Alias count is 0");

    // Now that all RW aliases have been unregistered, an RX alias to PA 0x00050000 is permitted
    TEST_ASSERT(vm_map.check_alias_wx(0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "RX alias is now permitted after all RW aliases are unregistered");
    TEST_ASSERT(vm_map.register_physical_alias(0x00070000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtExecute, &err),
                "Register RX alias succeeds");
    TEST_ASSERT(vm_map.alias_count() == 1, "Alias count is 1");

    // Now a new RW alias must be rejected
    TEST_ASSERT(!vm_map.register_physical_alias(0x00080000ULL, 0x00050000ULL, 0x4000ULL, kVmProtRead | kVmProtWrite, &err),
                "New RW alias to existing RX physical frame must be rejected");

    std::cout << "[Stage 254.7] W^X alias tracking and reclamation hardening verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 254.8: Six-Core AIC2 & Exception Regression Baseline
// ----------------------------------------------------------------------------
static void test_stage254_8_aic2_exception_regression_baseline() {
    std::cout << "[Stage 254.8] Testing Six-Core AIC2 & Exception Regression Baseline...\n";

    // Instantiate 6-core Apple AIC2 (A16 Bionic: 2 P-cores + 4 E-cores)
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);
    TEST_ASSERT(aic.core_count() == 6, "AIC2 core count is 6");

    // Verify Hardware Info Register
    uint64_t info = aic.read_register(AppleAIC::kRegInfo, 4);
    TEST_ASSERT(info == 0x06010200u, "AIC_INFO returns version 2, 6 cores (0x06010200)");

    // Enable AIC Global Config
    aic.write_register(AppleAIC::kRegConfig, 1, 4);
    TEST_ASSERT(aic.read_register(AppleAIC::kRegConfig, 4) == 1, "AIC_CONFIG is enabled");

    // Per-core verification: All 6 cores initialized clean
    for (std::size_t core = 0; core < 6; ++core) {
        uint64_t core_base = AppleAIC::kRegCoreBase + core * AppleAIC::kCoreStride;
        uint64_t whoami = aic.read_register(core_base + AppleAIC::kCoreOffWhoAmI, 4);
        TEST_ASSERT(whoami == core, "Core WhoAmI matches core index");
        TEST_ASSERT(!aic.has_pending_interrupt(core), "Core has no pending interrupt initially");

        // Popping event when none pending returns kEventNone (0xFFFFFFFF)
        uint64_t empty_event = aic.read_register(core_base + AppleAIC::kCoreOffEvent, 4);
        TEST_ASSERT(empty_event == AppleAIC::kEventNone, "Empty event queue returns kEventNone");
    }

    // Test Inter-Processor Interrupt (IPI): Core 0 sends IPI to Core 5 (Efficiency core)
    aic.send_ipi(1u << 5);
    TEST_ASSERT(aic.has_pending_interrupt(5), "Core 5 has pending interrupt after IPI");
    TEST_ASSERT(!aic.has_pending_interrupt(0), "Core 0 has no pending interrupt");

    // Core 5 pops the event:
    uint64_t core5_base = AppleAIC::kRegCoreBase + 5 * AppleAIC::kCoreStride;
    uint64_t event = aic.read_register(core5_base + AppleAIC::kCoreOffEvent, 4);
    uint32_t event_type = static_cast<uint32_t>(event >> 16);
    TEST_ASSERT(event_type == AppleAIC::kEventTypeIpi, "Popped event is IPI type (0x0001)");

    // Core 5 signals End of Interrupt (EOI)
    aic.write_register(core5_base + AppleAIC::kCoreOffEvent, event, 4);
    TEST_ASSERT(!aic.has_pending_interrupt(5), "Pending interrupt cleared on Core 5 after EOI");

    // Verify ARM64 Exception Vector Alignment and Structure
    // VBAR_EL1 must be 2048-byte aligned (bits [10:0] == 0)
    const uint64_t vbar_el1 = 0x0000000000008000ULL;
    TEST_ASSERT((vbar_el1 % KernelExceptionVectors::kVectorTableAlignment) == 0,
                "VBAR_EL1 satisfies 2048-byte architectural alignment");

    // Vector table layout verification:
    // Offset for Current EL with SP_ELx, IRQ vector = 0x280
    uint64_t irq_offset = KernelExceptionVectors::compute_vector_offset(
        ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::IRQ);
    TEST_ASSERT(irq_offset == 0x280ULL, "CurrentEL_SPx IRQ offset is exactly 0x280");

    uint64_t sync_offset = KernelExceptionVectors::compute_vector_offset(
        ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::Synchronous);
    TEST_ASSERT(sync_offset == 0x200ULL, "CurrentEL_SPx Synchronous offset is 0x200");

    std::cout << "[Stage 254.8] Six-core AIC2 & exception regression baseline verified.\n";
}

int main() {
    std::cout << "====================================================================\n";
    std::cout << "Campaign 254: Stages 254.5 - 254.8 MMU, W^X & AIC2 Regression Tests\n";
    std::cout << "====================================================================\n";

    test_stage254_5_kernel_dt_bootargs_ownership();
    test_stage254_6_mmu_ownership_cross_validation();
    test_stage254_7_wx_alias_reclamation_hardening();
    test_stage254_8_aic2_exception_regression_baseline();

    std::cout << "====================================================================\n";
    std::cout << "All Stages 254.5 - 254.8 Tests PASSED Successfully!\n";
    std::cout << "====================================================================\n";
    return 0;
}
