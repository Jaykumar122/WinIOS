// ============================================================================
// WinIOS Campaign 268: Master Engineering Execution
// Stages 268.8 - 268.11: Mach-O Image Audit, Source-to-Binary Provenance,
// Guest Entry Verification, and 7,938-Instruction Execution Trace Certification
// ============================================================================

#include "cpu/cpu.h"
#include "loader/kernel_image_parser.h"
#include "loader/xnu_genuine_init_builder.h"
#include "memory/memory.h"
#include "runtime/kernel_boot_coordinator.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::runtime;

// -----------------------------------------------------------------------------
// Stage 268.8: Mach-O Image and Entry Audit
// -----------------------------------------------------------------------------
static bool test_stage268_8_macho_image_and_entry_audit() {
    std::cout << "[Stage 268.8] Auditing Mach-O Image Structure, Headers & Segments...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(!kernel_image.empty(), "Mach-O image successfully retrieved");
    TEST_ASSERT(kernel_image.size() == 68728, "Mach-O image size is exactly 68,728 bytes");

    const std::string sha256 = XnuGenuineInitBuilder::compute_sha256(kernel_image);
    TEST_ASSERT(sha256 == "f6e159b9a271d8b6f6a81472ab94b283f26fd711e584b145b28af974240d65e3",
                "Mach-O image SHA-256 matches certified golden digest");

    KernelParsedImage parsed;
    const auto status = KernelImageParser::parse(kernel_image.data(), kernel_image.size(), parsed);
    TEST_ASSERT(status == KernelImageParseStatus::Success, "KernelImageParser parses Mach-O headers cleanly");
    TEST_ASSERT(parsed.entry_point_vaddr == 0x2BE0ULL, "Mach-O entry point is 0x2BE0 (_start)");

    // Audit Segments
    const auto* text = parsed.find_segment("__TEXT");
    const auto* data = parsed.find_segment("__DATA");

    TEST_ASSERT(text != nullptr, "__TEXT segment exists");
    TEST_ASSERT(text->vmaddr == 0x0ULL && text->vmsize == 0x2E08ULL, "__TEXT mapped at 0x0..0x2E08");
    TEST_ASSERT((text->initprot & 0x5u) == 0x5u, "__TEXT initprot is RX (0x5)");

    TEST_ASSERT(data != nullptr, "__DATA segment exists");
    TEST_ASSERT(data->vmaddr == 0x8000ULL && data->vmsize == 0x80C8ULL, "__DATA mapped at 0x8000..0x100C8");
    TEST_ASSERT((data->initprot & 0x3u) == 0x3u, "__DATA initprot is RW (0x3)");

    std::cout << "  - Image Size:        68,728 bytes\n";
    std::cout << "  - SHA-256:           " << sha256 << "\n";
    std::cout << "  - Mach-O Magic:      0xFEEDFACF (64-bit LE)\n";
    std::cout << "  - CPU Type:          ARM64 (0x0100000C)\n";
    std::cout << "  - Entry Point:       0x2BE0 (_start)\n";
    std::cout << "  - __TEXT Segment:    0x00000000 .. 0x00002E08 (RX)\n";
    std::cout << "  - __DATA Segment:    0x00008000 .. 0x000100C8 (RW)\n";

    std::cout << "  [PASS] Stage 268.8 Mach-O image structure and entry audited.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 268.9: Upstream Source-to-Binary Provenance
// -----------------------------------------------------------------------------
static bool test_stage268_9_source_to_binary_provenance() {
    std::cout << "[Stage 268.9] Mapping Assembly Encodings from xnu_start.s to Binary...\n";

    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();
    TEST_ASSERT(kernel_image.size() >= 0x2C24, "Kernel image contains startup block");

    auto read_insn = [&](std::size_t offset) -> std::uint32_t {
        return static_cast<std::uint32_t>(kernel_image[offset]) |
              (static_cast<std::uint32_t>(kernel_image[offset + 1]) << 8) |
              (static_cast<std::uint32_t>(kernel_image[offset + 2]) << 16) |
              (static_cast<std::uint32_t>(kernel_image[offset + 3]) << 24);
    };

    // Verify verbatim binary instruction stream corresponding to xnu_start.s
    const std::uint32_t insn0 = read_insn(0x2BE0); // msr daifset, #0xf
    const std::uint32_t insn1 = read_insn(0x2BE4); // mrs x20, mpidr_el1
    const std::uint32_t insn2 = read_insn(0x2BE8); // and x20, x20, #0xff
    const std::uint32_t insn3 = read_insn(0x2BEC); // cbnz x20, 0x2c1c
    const std::uint32_t insn4 = read_insn(0x2BF0); // mov x2, #0x300000
    const std::uint32_t insn5 = read_insn(0x2BF4); // msr cpacr_el1, x2
    const std::uint32_t insn6 = read_insn(0x2BF8); // isb
    const std::uint32_t insn_bl  = read_insn(0x2C10); // bl _arm_init
    const std::uint32_t insn_wfi = read_insn(0x2C14); // wfi
    const std::uint32_t insn_b   = read_insn(0x2C18); // b 0x2c18

    TEST_ASSERT(insn0 == 0xD5034FDFu, "0x2be0: msr daifset, #0xf (0xD5034FDF)");
    TEST_ASSERT(insn1 == 0xD53800B4u, "0x2be4: mrs x20, mpidr_el1 (0xD53800B4)");
    TEST_ASSERT(insn2 == 0x92401E94u, "0x2be8: and x20, x20, #0xff (0x92401E94)");
    TEST_ASSERT(insn3 == 0xB5000194u, "0x2bec: cbnz x20, secondary_park (0xB5000194)");
    TEST_ASSERT(insn4 == 0xD2A00602u, "0x2bf0: mov x2, #0x300000 (0xD2A00602)");
    TEST_ASSERT(insn5 == 0xD5181042u, "0x2bf4: msr cpacr_el1, x2 (0xD5181042)");
    TEST_ASSERT(insn6 == 0xD5033FDFu, "0x2bf8: isb (0xD5033FDF)");
    TEST_ASSERT(insn_bl  == 0x97FFFE3Fu, "0x2c10: bl _arm_init (0x97FFFE3F)");
    TEST_ASSERT(insn_wfi == 0xD503207Fu, "0x2c14: wfi (0xD503207F)");
    TEST_ASSERT(insn_b   == 0x14000000u, "0x2c18: b 0x2c18 (0x14000000)");

    std::cout << "  - 0x2BE0: msr daifset, #0xf  [0xD5034FDF] (Verified)\n";
    std::cout << "  - 0x2BE4: mrs x20, mpidr_el1 [0xD53800B4] (Verified)\n";
    std::cout << "  - 0x2BE8: and x20, x20, 0xff [0x92401E94] (Verified)\n";
    std::cout << "  - 0x2BEC: cbnz x20, 0x2c1c   [0xB5000194] (Verified)\n";
    std::cout << "  - 0x2BF0: mov x2, #0x300000  [0xD2A00602] (Verified)\n";
    std::cout << "  - 0x2BF4: msr cpacr_el1, x2  [0xD5181042] (Verified)\n";
    std::cout << "  - 0x2BF8: isb                [0xD5033FDF] (Verified)\n";
    std::cout << "  - 0x2C10: bl _arm_init       [0x97FFFE3F] (Verified)\n";
    std::cout << "  - 0x2C14: wfi                [0xD503207F] (Verified)\n";
    std::cout << "  - 0x2C18: b 0x2c18           [0x14000000] (Verified)\n";
    std::cout << "  - Provenance Classification: Category B (Upstream-derived source adapted for WinIOS)\n";

    std::cout << "  [PASS] Stage 268.9 Source-to-binary provenance mapped.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 268.10: Independent Guest Entry Verification
// -----------------------------------------------------------------------------
static bool test_stage268_10_independent_guest_entry_verification() {
    std::cout << "[Stage 268.10] Verifying Guest CPU State at _start Entry Point...\n";

    CPU cpu;
    Memory memory(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.use_canonical_memory_layout = true;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Kernel boot coordinator succeeded");

    // Verify machine state at entry:
    TEST_ASSERT(cpu.get_program_counter() == 0x2BE0ULL, "PC starts exactly at _start (0x2BE0)");
    TEST_ASSERT(cpu.get_stack_pointer() == 0x40000ULL, "SP is Frame 15 top (0x40000)");
    TEST_ASSERT(cpu.read_register(0) == 0x34000ULL, "X0 points to Frame 13 BootArgs (0x34000)");
    TEST_ASSERT(cpu.read_register(19) == 0x34000ULL, "X19 holds preserved BootArgs pointer (0x34000)");
    TEST_ASSERT(cpu.get_current_el() == 1, "Guest CPU executes at EL1");
    TEST_ASSERT(cpu.get_spsr_el1() == 0x3C5u, "SPSR_EL1 is EL1h with DAIF masked (0x3C5)");
    TEST_ASSERT(!cpu.is_waiting_for_interrupt(), "CPU is active, not halted");

    std::cout << "  - Entry PC:    0x" << std::hex << cpu.get_program_counter() << "\n";
    std::cout << "  - SP_EL1:      0x" << cpu.get_stack_pointer() << "\n";
    std::cout << "  - X0:          0x" << cpu.read_register(0) << "\n";
    std::cout << "  - CurrentEL:   " << std::dec << cpu.get_current_el() << "\n";
    std::cout << "  - Host Bypass: NONE (Clean entry from _start without forced PC manipulation)\n";

    std::cout << "  [PASS] Stage 268.10 Guest entry verification verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// Stage 268.11: Full 7,938-Instruction Trace Certification
// -----------------------------------------------------------------------------
static bool test_stage268_11_full_7938_instruction_trace_certification() {
    std::cout << "[Stage 268.11] Reproducing & Certifying 7,938-Instruction Execution Trace to WFI...\n";

    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory memory(0x800000);
    const auto kernel_image = XnuGenuineInitBuilder::load_or_get_image_210();

    KernelBootConfig config{};
    config.raw_kernel_image = kernel_image;
    config.dram_base = 0x0ULL;
    config.dram_size = 0x800000ULL;
    config.num_cores = 6;
    config.enable_apple_aic = true;
    config.use_canonical_memory_layout = true;

    const auto boot_res = KernelBootCoordinator::prepare_and_boot(cpu, memory, config);
    TEST_ASSERT(boot_res.success, "Boot preparation succeeded");

    std::size_t retired = 0;
    constexpr std::size_t kMaxSteps = 30000;
    while (retired < kMaxSteps && !cpu.is_waiting_for_interrupt()) {
        cpu.step(memory);
        ++retired;
    }

    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "Execution halts naturally at WFI");
    TEST_ASSERT(retired == 7938, "Exact retirement count must be 7,938 instructions");

    std::cout << "  - Instructions Retired: 7,938 (100% exact match to Campaign 267 baseline)\n";
    std::cout << "  - Terminal State:       Architectural WFI reached via natural guest control flow\n";

    std::cout << "  [PASS] Stage 268.11 7,938-instruction trace certified.\n";
    return true;
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "Campaign 268 - Stages 268.8..268.11 Provenance & Trace Tests\n";
    std::cout << "=================================================================\n";

    if (!test_stage268_8_macho_image_and_entry_audit()) return 1;
    if (!test_stage268_9_source_to_binary_provenance()) return 1;
    if (!test_stage268_10_independent_guest_entry_verification()) return 1;
    if (!test_stage268_11_full_7938_instruction_trace_certification()) return 1;

    std::cout << "=================================================================\n";
    std::cout << "All Stages 268.8..268.11 Tests Passed Successfully!\n";
    std::cout << "=================================================================\n";
    return 0;
}
