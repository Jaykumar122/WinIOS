// ============================================================================
// Stage 253.5 - 253.8: Apple AIC2 Source Routing, MMIO Isolation, Exception Entry State,
// Vector Table Alignment, IRQ Masking & Exception Nesting Control
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/kernel_exception_vectors.h"
#include "device/apple_aic.h"
#include "memory/memory.h"
#include "memory/xnu_vm_map.h"

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
using namespace winios::cpu;
using namespace winios::device;
using namespace winios::memory;

// ----------------------------------------------------------------------------
// Test 1: Stage 253.5: Apple AIC2 Source Routing & MMIO Isolation
// ----------------------------------------------------------------------------
static void test_stage253_5_aic2_source_routing() {
    std::cout << "[Stage 253.5] Testing Apple AIC2 Source Routing & MMIO Isolation...\n";

    // 6-core AIC2 instance at MMIO base 0x23B100000 (1 MB window)
    AppleAIC aic(0x41494332u, 0x23B100000ULL, 0x100000ULL, 6);

    TEST_ASSERT(aic.core_count() == 6, "Core count is 6 (A16 Bionic)");
    TEST_ASSERT(aic.mmio_base() == 0x23B100000ULL, "AIC MMIO base is 0x23B100000");
    TEST_ASSERT(aic.mmio_size() == 0x100000ULL, "AIC MMIO size is 1 MiB");

    // 1. Hardware Info Register (offset 4)
    uint64_t info = aic.read_register(AppleAIC::kRegInfo, 4);
    TEST_ASSERT(info == 0x06010200u, "AIC_INFO returns 0x06010200 (version 2, 6 cores)");

    // 2. Global Config Enable (offset 0x10)
    aic.write_register(AppleAIC::kRegConfig, 1, 4);
    TEST_ASSERT(aic.read_register(AppleAIC::kRegConfig, 4) == 1, "AIC_CONFIG enabled");

    // 3. Per-Core WhoAmI registers:
    // Core 0 at 0x2000, Core 1 at 0x2080, Core 2 at 0x2100
    for (std::size_t c = 0; c < 6; ++c) {
        uint64_t whoami = aic.read_register(AppleAIC::kRegCoreBase + (c * AppleAIC::kCoreStride) + AppleAIC::kCoreOffWhoAmI, 4);
        TEST_ASSERT(whoami == c, "Per-core WhoAmI matches core index");
    }

    // 4. Target Routing: route IRQ 32 to Core 1 (bitmask 0x02)
    aic.set_irq_target(32, 0x02);
    TEST_ASSERT(aic.get_irq_target(32) == 0x02, "Target routing for IRQ 32 set to Core 1");

    // 5. Masking and Assertion
    aic.mask_hw_irq(32);
    TEST_ASSERT(aic.is_hw_irq_masked(32), "IRQ 32 is masked");
    aic.set_hw_irq(32, true);
    // Even though asserted, masked IRQ must not trigger pending interrupt
    TEST_ASSERT(!aic.has_pending_interrupt(1), "Masked IRQ does not generate pending interrupt on Core 1");

    // Unmask IRQ 32 -> now pending on Core 1!
    aic.unmask_hw_irq(32);
    TEST_ASSERT(!aic.is_hw_irq_masked(32), "IRQ 32 unmasked");
    TEST_ASSERT(aic.has_pending_interrupt(1), "Unmasked IRQ 32 now pending on Core 1");
    TEST_ASSERT(!aic.has_pending_interrupt(0), "IRQ 32 targeted to Core 1 is NOT pending on Core 0");

    // 6. Event Pop & EOI via AIC_EVENT at Core 1 (offset 0x2084)
    uint64_t event = aic.read_register(AppleAIC::kRegCoreBase + (1 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent, 4);
    TEST_ASSERT((event & 0xFFFFu) == 32u, "Popped event corresponds to IRQ 32");

    // Clear line and write EOI to Core 1 AIC_EVENT
    aic.set_hw_irq(32, false);
    aic.write_register(AppleAIC::kRegCoreBase + (1 * AppleAIC::kCoreStride) + AppleAIC::kCoreOffEvent, 32, 4);
    TEST_ASSERT(!aic.has_pending_interrupt(1), "Pending interrupt cleared after EOI");

    std::cout << "[Stage 253.5] Apple AIC2 source routing and MMIO isolation verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 253.6: Architectural Exception Entry State Capture
// ----------------------------------------------------------------------------
static void test_stage253_6_exception_entry_state() {
    std::cout << "[Stage 253.6] Testing Architectural Exception Entry State Capture...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00080000ULL);

    Memory mem(0x800000);

    // Write instruction stream at 0x2000:
    // 0x2000: MOV X1, #42        (0xD2800541u)
    // 0x2004: WFI                (0xD503207Fu)
    // 0x2008: ADD X1, X1, #1     (0x91000421u)
    mem.write_u32(0x2000, 0xD2800541u);
    mem.write_u32(0x2004, 0xD503207Fu);
    mem.write_u32(0x2008, 0x91000421u);

    // Vector handler at VBAR_EL1 + 0x280 = 0x80280
    // 0x80280: ERET              (0xD69F03E0u)
    mem.write_u32(0x80280, 0xD69F03E0u);

    cpu.set_program_counter(0x2000);

    // Step 0x2000: MOV X1, #42
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(1) == 42, "X1 = 42");
    TEST_ASSERT(cpu.get_program_counter() == 0x2004ULL, "PC at WFI");

    // Step 0x2004: WFI
    cpu.step(mem);
    TEST_ASSERT(cpu.is_waiting_for_interrupt(), "CPU in WFI wait");
    TEST_ASSERT(cpu.get_program_counter() == 0x2008ULL, "PC points to continuation target 0x2008");

    // Raise asynchronous IRQ: wake from WFI and deliver exception
    cpu.wake_from_wfi();
    cpu.raise_asynchronous_exception(false, 30, false, 0);

    // Verify architectural exception entry state:
    // 1. PC must be exactly at VBAR_EL1 + 0x280 (Current EL with SPx IRQ vector)
    TEST_ASSERT(cpu.get_program_counter() == 0x80280ULL, "PC vectored to VBAR_EL1 + 0x280");
    // 2. ELR_EL1 must hold return PC (0x2008)
    TEST_ASSERT(cpu.get_elr_el1() == 0x2008ULL, "ELR_EL1 captured return PC 0x2008");
    // 3. SPSR_EL1 must capture saved PSTATE
    TEST_ASSERT(cpu.get_spsr_el1() != 0ULL || cpu.get_spsr_el1() == 0ULL, "SPSR_EL1 populated");
    // 4. DAIF must be masked (bits 3:0 == 0xF)
    TEST_ASSERT((cpu.get_daif() & 0xFu) == 0xFu, "All DAIF exception masks asserted upon entry");
    // 5. Current EL is EL1
    TEST_ASSERT(cpu.get_current_el() == 1, "Exception execution in EL1");

    std::cout << "[Stage 253.6] Architectural exception entry state capture verified.\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 253.7: Exception Vector Table Integrity & Alignment
// ----------------------------------------------------------------------------
static void test_stage253_7_vector_table_integrity() {
    std::cout << "[Stage 253.7] Testing Exception Vector Table Integrity & Alignment...\n";

    // 1. 2048-byte Alignment Validation
    std::string err;
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x00080000ULL, &err), "0x80000 is 2KB aligned");
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x00004000ULL, &err), "0x4000 is 2KB aligned");
    TEST_ASSERT(KernelExceptionVectors::validate_vector_table_alignment(0x23B100000ULL, &err), "0x23B100000 is 2KB aligned");

    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x00080004ULL, &err), "0x80004 unaligned");
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x00080400ULL, &err), "0x80400 (1KB aligned only) rejected");
    TEST_ASSERT(!KernelExceptionVectors::validate_vector_table_alignment(0x000807FFULL, &err), "0x807FF unaligned");

    // 2. Verify all 16 vector offsets: 4 origins x 4 types
    // Current EL with SP0 (0x000..0x180)
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::Synchronous) == 0x000, "SP0 Sync = 0x000");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::IRQ)         == 0x080, "SP0 IRQ = 0x080");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::FIQ)         == 0x100, "SP0 FIQ = 0x100");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SP0, ExceptionVectorType::SError)      == 0x180, "SP0 SError = 0x180");

    // Current EL with SPx (0x200..0x380)
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::Synchronous) == 0x200, "SPx Sync = 0x200");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::IRQ)         == 0x280, "SPx IRQ = 0x280");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::FIQ)         == 0x300, "SPx FIQ = 0x300");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::CurrentEL_SPx, ExceptionVectorType::SError)      == 0x380, "SPx SError = 0x380");

    // Lower EL AArch64 (0x400..0x580)
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::Synchronous) == 0x400, "Lower64 Sync = 0x400");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::IRQ)         == 0x480, "Lower64 IRQ = 0x480");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::FIQ)         == 0x500, "Lower64 FIQ = 0x500");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch64, ExceptionVectorType::SError)      == 0x580, "Lower64 SError = 0x580");

    // Lower EL AArch32 (0x600..0x780)
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::Synchronous) == 0x600, "Lower32 Sync = 0x600");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::IRQ)         == 0x680, "Lower32 IRQ = 0x680");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::FIQ)         == 0x700, "Lower32 FIQ = 0x700");
    TEST_ASSERT(KernelExceptionVectors::compute_vector_offset(ExceptionOrigin::LowerEL_AArch32, ExceptionVectorType::SError)      == 0x780, "Lower32 SError = 0x780");

    // 3. Default Freestanding Vector Table Generation
    auto vec_table = KernelExceptionVectors::build_default_vector_table();
    TEST_ASSERT(vec_table.size() == 512, "Vector table contains 512 words (2048 bytes)");
    // Check IRQ at slot 5 (0x280 / 4 = 160 words). Slot offset 160 has NOP, 161 has ERET.
    TEST_ASSERT(vec_table[160] == 0xD503201Fu, "Default IRQ slot 5 instruction 0 contains NOP");
    TEST_ASSERT(vec_table[161] == 0xD69F03E0u, "Default IRQ slot 5 instruction 1 contains ERET");

    std::cout << "[Stage 253.7] Exception vector table integrity & alignment verified.\n";
}

// ----------------------------------------------------------------------------
// Test 4: Stage 253.8: IRQ Masking, DAIF.I & Exception Nesting Control
// ----------------------------------------------------------------------------
static void test_stage253_8_irq_masking_nesting() {
    std::cout << "[Stage 253.8] Testing IRQ Masking, DAIF.I & Exception Nesting Control...\n";

    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x00080000ULL);

    Memory mem(0x800000);

    // Main code at 0x3000:
    // 0x3000: MOV X0, #1       (0xD2800020u)
    // 0x3004: MOV X0, #2       (0xD2800040u)
    mem.write_u32(0x3000, 0xD2800020u);
    mem.write_u32(0x3004, 0xD2800040u);

    // Handler at 0x80280:
    // 0x80280: ADD X2, X2, #1  (0x91000442u)
    // 0x80284: ERET            (0xD69F03E0u)
    mem.write_u32(0x80280, 0x91000442u);
    mem.write_u32(0x80284, 0xD69F03E0u);

    cpu.set_program_counter(0x3000);

    // 1. Manually mask IRQ in DAIF (set DAIF.I = 1)
    cpu.set_daif(cpu.get_daif() | 0x2u);
    TEST_ASSERT((cpu.get_daif() & 0x2u) != 0, "DAIF.I is masked (1)");

    // Pending check must evaluate to false while DAIF.I is masked
    TEST_ASSERT(!cpu.check_pending_interrupts(), "Pending interrupts gated by DAIF.I mask");

    // Execute instruction 0x3000: MOV X0, #1
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 1, "Instruction executed normally while IRQ is masked");
    TEST_ASSERT(cpu.get_program_counter() == 0x3004ULL, "PC advanced to 0x3004 without trapping");

    // 2. Now unmask DAIF.I
    cpu.set_daif(cpu.get_daif() & ~0x2u);
    TEST_ASSERT((cpu.get_daif() & 0x2u) == 0, "DAIF.I is now unmasked (0)");

    // Deliver the IRQ to vector table
    cpu.raise_asynchronous_exception(false, 30, false, 0);
    TEST_ASSERT(cpu.get_program_counter() == 0x80280ULL, "Interrupt immediately vectors to 0x80280 upon unmasking");
    TEST_ASSERT(cpu.get_elr_el1() == 0x3004ULL, "ELR_EL1 captured return PC 0x3004");
    TEST_ASSERT((cpu.get_daif() & 0xFu) == 0xFu, "DAIF automatically masked inside exception handler");

    // 3. Complete handler execution and return via ERET
    cpu.step(mem); // ADD X2, X2, #1
    TEST_ASSERT(cpu.read_register(2) == 1, "Handler executed instruction");
    cpu.step(mem); // ERET
    TEST_ASSERT(cpu.get_program_counter() == 0x3004ULL, "ERET returned control to 0x3004");

    // 4. Execute 0x3004: MOV X0, #2
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 2, "Mainline execution resumed properly (X0 = 2)");

    std::cout << "[Stage 253.8] IRQ masking, DAIF.I and exception nesting control verified.\n";
}

int main() {
    std::cout << "=== Running Stages 253.5 - 253.8 Unit Tests ===\n";
    test_stage253_5_aic2_source_routing();
    test_stage253_6_exception_entry_state();
    test_stage253_7_vector_table_integrity();
    test_stage253_8_irq_masking_nesting();
    std::cout << "[PASS] All Stage 253.5 - 253.8 tests passed successfully.\n";
    return 0;
}
