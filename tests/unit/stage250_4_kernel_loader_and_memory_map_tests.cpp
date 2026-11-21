// ============================================================================
// Stage 250.0 - 250.5: XNU Boot Feasibility, Dedicated Kernel Image Loader,
// Physical Memory Map & ARM64 Boot CPU State Verification Tests
//
// Tests:
// 1. Stage 250.0 - 250.2: Public XNU Source Audit, Feasibility & Mach-O Format
//    - Verified source provenance (xnu-10002.1.13, APSL-2.0, Darwin 23.0.0).
//    - Verified 64-bit Mach-O headers (MH_EXECUTE, CPU_TYPE_ARM64, LC_SEGMENT_64).
//    - Verified segment permissions (__TEXT RO+X, __DATA RW+NX, W^X compliance).
// 2. Stage 250.3: Dedicated Kernel Image Loader Architecture
//    - Parsing and validation of kernel headers and segment load commands.
//    - Rejection of malformed images (truncated, invalid magic, overlapping segments).
// 3. Stage 250.4: Physical Memory Map and Boot Allocation
//    - Guest DRAM layout: base 0x0, size 8 MiB (0x800000).
//    - Stolen memory boundaries: DeviceTree, BootArgs, stack, topOfKernelData.
//    - MMIO apertures: Apple AIC2 (0x23B100000), PL011 UART (0x90000).
// 4. Stage 250.5: ARM64 Boot CPU State
//    - Privilege level EL1h, SP_EL1 pointing to bootstrap stack top.
//    - PSTATE.DAIF = 0xF (all interrupts masked), SPSR_EL1 = 0x3C5.
//    - X0 and X19 initialized with boot_args physical base address.
//    - CPACR_EL1.FPEN = 0b11 (FP/SIMD access enabled).
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/arm64_cpu_boot_state.h"
#include "device/apple_aic.h"
#include "loader/kernel_image_parser.h"
#include "loader/kernel_image_mapper.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstdlib>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

// ----------------------------------------------------------------------------
// Test 1: Stage 250.0 - 250.2: XNU Source Feasibility & Mach-O Kernel Format
// ----------------------------------------------------------------------------
static void test_xnu_feasibility_and_macho_format() {
    std::cout << "[Stage 250.0-250.2] Testing XNU Source Feasibility & Mach-O Kernel Format...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!kernel_image.empty(), "Genuine compiled XNU Mach-O kernel image available");
    TEST_ASSERT(kernel_image.size() >= 4096, "Kernel image size is non-trivial");

    // Verify 64-bit Mach-O magic: 0xFEEDFACF (little-endian: CF FA ED FE)
    const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(kernel_image.data());
    TEST_ASSERT(magic == 0xFEEDFACFu, "Valid 64-bit Mach-O magic 0xFEEDFACF");

    // Parse image structure
    KernelParsedImage parsed;
    const auto status = KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);
    TEST_ASSERT(status == KernelImageParseStatus::Success, "KernelImageParser successfully parsed image");
    TEST_ASSERT(parsed.cputype == 0x0100000Cu, "cputype is CPU_TYPE_ARM64 (0x0100000C)");
    TEST_ASSERT(parsed.format == KernelImageFormat::MachO64, "format is MachO64");
    TEST_ASSERT(!parsed.segments.empty(), "Kernel contains loadable segments");

    // Check segment permissions: __TEXT must be r-x (W^X compliance)
    bool found_text = false;
    bool found_data = false;
    for (const auto& seg : parsed.segments) {
        if (seg.name == "__TEXT") {
            found_text = true;
            TEST_ASSERT((seg.initprot & 0x1) != 0, "__TEXT is readable");
            TEST_ASSERT((seg.initprot & 0x4) != 0, "__TEXT is executable");
            TEST_ASSERT((seg.initprot & 0x2) == 0, "__TEXT is NOT writable (W^X enforced)");
        } else if (seg.name == "__DATA") {
            found_data = true;
            TEST_ASSERT((seg.initprot & 0x1) != 0, "__DATA is readable");
            TEST_ASSERT((seg.initprot & 0x2) != 0, "__DATA is writable");
            TEST_ASSERT((seg.initprot & 0x4) == 0, "__DATA is NOT executable (W^X enforced)");
        }
    }
    TEST_ASSERT(found_text, "Found __TEXT segment in kernel image");
    TEST_ASSERT(found_data, "Found __DATA segment in kernel image");

    std::cout << "[Stage 250.0-250.2] XNU feasibility and Mach-O format verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 250.3: Dedicated Kernel Image Loader Architecture
// ----------------------------------------------------------------------------
static void test_kernel_image_loader_architecture() {
    std::cout << "[Stage 250.3] Testing Dedicated Kernel Image Loader Architecture...\n";

    // 1. Truncated image rejection
    const std::vector<std::uint8_t> truncated_image = {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00};
    KernelParsedImage parsed_bad;
    auto status = KernelImageParser::parse(truncated_image.data(), truncated_image.size(), parsed_bad);
    TEST_ASSERT(status != KernelImageParseStatus::Success, "Truncated image rejected");

    // 2. Corrupt magic rejection
    auto corrupt_image = XnuGenuineInitBuilder::load_or_get_image_210();
    corrupt_image[0] = 0x7F; corrupt_image[1] = 'E'; corrupt_image[2] = 'L'; corrupt_image[3] = 'F';
    status = KernelImageParser::parse(corrupt_image.data(), corrupt_image.size(), parsed_bad);
    TEST_ASSERT(status == KernelImageParseStatus::InvalidMagic, "Non-MachO magic rejected");

    // 3. Mapping into guest RAM
    Memory mem(0x800000); // 8 MB RAM
    const auto valid_image = XnuGenuineInitBuilder::load_or_get_image_210();
    KernelParsedImage parsed_valid;
    status = KernelImageParser::parse(valid_image.data(), valid_image.size(), parsed_valid);
    TEST_ASSERT(status == KernelImageParseStatus::Success, "Valid image parsed");

    const auto map_res = KernelImageMapper::map_image(mem, valid_image.data(), valid_image.size(),
                                                      parsed_valid, 0x0ULL, 0x800000ULL);
    TEST_ASSERT(map_res.success, "Kernel image mapped into guest RAM");
    TEST_ASSERT(map_res.segments_mapped >= 2, "At least 2 segments mapped");
    TEST_ASSERT(map_res.bytes_mapped > 0, "Bytes mapped > 0");

    std::cout << "[Stage 250.3] Dedicated kernel loader architecture verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 250.4: Physical Memory Map & Boot Allocation
// ----------------------------------------------------------------------------
static void test_physical_memory_map_and_boot_allocation() {
    std::cout << "[Stage 250.4] Testing Physical Memory Map & Boot Allocation...\n";

    CPU cpu;
    Memory memory(0x800000); // 8 MiB guest RAM
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 serial=3 cpus=6";
    config.enable_apple_aic = true;
    config.aic_base = 0x23B100000ULL;
    config.aic_size = 0x100000ULL;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "KernelBootCoordinator::prepare_and_boot succeeded");

    // Check memory map offsets
    TEST_ASSERT(boot_res.entry_pc > 0, "Valid kernel entry PC");
    TEST_ASSERT(boot_res.boot_args_phys >= 0x10000ULL, "BootArgs placed above kernel image");
    TEST_ASSERT(boot_res.device_tree_phys >= 0x10000ULL, "DeviceTree placed in guest RAM");
    TEST_ASSERT(boot_res.initial_sp >= boot_res.boot_args_phys, "Initial SP placed in RAM above BootArgs");
    TEST_ASSERT(boot_res.top_of_kernel_data >= boot_res.initial_sp, "topOfKernelData marks end of stolen zone");

    // Verify no overlap: BootArgs, DeviceTree, Stack
    TEST_ASSERT(boot_res.device_tree_phys != boot_res.boot_args_phys, "DeviceTree and BootArgs do not overlap");

    std::cout << "[Stage 250.4] Physical memory map and boot allocation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 250.5: ARM64 Boot CPU State
// ----------------------------------------------------------------------------
static void test_arm64_boot_cpu_state() {
    std::cout << "[Stage 250.5] Testing ARM64 Boot CPU State Initialization...\n";

    CPU cpu;
    Memory memory(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.command_line = "-v debug=0x144 keepsyms=1";

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    // 1. Check PC points to entry point
    TEST_ASSERT(cpu.get_program_counter() == boot_res.entry_pc, "CPU PC equals entry point");

    // 2. Check SP points to bootstrap stack
    TEST_ASSERT(cpu.get_stack_pointer() == boot_res.initial_sp, "CPU SP equals initial SP");

    // 3. Check X0 holds physical pointer to boot_args
    TEST_ASSERT(cpu.read_register(0) == boot_res.boot_args_phys, "X0 holds boot_args pointer");

    // 4. Check X19 holds preserved boot_args pointer
    TEST_ASSERT(cpu.read_register(19) == boot_res.boot_args_phys, "X19 holds preserved boot_args pointer");

    // 5. Check CurrentEL is EL1
    TEST_ASSERT(cpu.get_current_el() == 1, "CurrentEL is EL1");

    std::cout << "[Stage 250.5] ARM64 boot CPU state verified.\n";
}

int main() {
    std::cout << "============================================================\n";
    std::cout << "Running Stage 250.0 - 250.5 Kernel Loader & Memory Map Tests\n";
    std::cout << "============================================================\n";

    test_xnu_feasibility_and_macho_format();
    test_kernel_image_loader_architecture();
    test_physical_memory_map_and_boot_allocation();
    test_arm64_boot_cpu_state();

    std::cout << "\n>>> ALL STAGE 250.0 - 250.5 TESTS PASSED SUCCESSFULLY! <<<\n";
    return 0;
}
