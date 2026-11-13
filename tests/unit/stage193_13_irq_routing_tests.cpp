// ============================================================================
// Stage 193.13: IRQ/FIQ Exception Routing Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/gicv3.h"
#include "memory/memory.h"
#include "cpu/synchronous_exception.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

using namespace winios;

static bool test_el0_irq_exception_routing_and_return() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL); // 4MB

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    // Configure GIC: Group 1 in Distributor and CPU interface
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4); // GICD_CTLR.EnableGrp0 | EnableGrp1
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0xFFFFFFFFu, 4); // GICD_IGROUPR1: Group 1
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // Setup user code at 0x1000: ADD X1, X1, #1 (0x91000421u)
    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_va), 0x91000421u);

    // Setup EL1 IRQ handler at vbar + 0x480 (Lower EL AArch64 IRQ vector):
    // 0x480: MRS X0, ICC_IAR1_EL1 (0xD530CC00u)
    // 0x484: MSR ICC_EOIR1_EL1, X0 (0xD510CC20u)
    // 0x488: ERET (0xD69F03E0u)
    const std::uint64_t irq_handler_va = vbar + 0x480ULL;
    mem.write_u32(static_cast<std::size_t>(irq_handler_va), 0xD530CC00u);
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 4u), 0xD510CC20u);
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 8u), 0xD69F03E0u);

    // Enable SPI INTID 36, Priority 0x20
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 4u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 4u, 0x20u, 1);
    gic.set_spi_level(36, true);

    // Set CPU to EL0, DAIF = 0 (interrupts unmasked)
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0);
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, 10);

    // Step 1: Interrupt preemption occurs. CPU vectors to vbar + 0x480
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must enter EL1 on IRQ");
    CHECK(cpu.get_program_counter() == irq_handler_va, "Must jump to Lower EL IRQ vector (VBAR + 0x480)");
    CHECK(cpu.get_elr_el1() == user_code_va, "ELR_EL1 must hold interrupted user PC");
    CHECK(cpu.get_daif() == 0xFu, "DAIF must be masked on exception entry");

    // Step 2: In handler, execute MRS X0, ICC_IAR1_EL1
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == irq_handler_va + 4u, "PC advanced past MRS");
    CHECK(cpu.read_register(0) == 36u, "X0 must hold acknowledged INTID 36");

    // Deassert SPI 36 before EOI so it does not re-pend
    gic.set_spi_level(36, false);

    // Step 3: In handler, execute MSR ICC_EOIR1_EL1, X0
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == irq_handler_va + 8u, "PC advanced past MSR EOIR");

    // Step 4: In handler, execute ERET
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 0, "Must return to EL0");
    CHECK(cpu.get_program_counter() == user_code_va, "PC must be restored to user_code_va (0x1000)");
    CHECK(cpu.get_daif() == 0, "DAIF must be restored to unmasked state (0)");

    // Step 5: User code executes ADD X1, X1, #1
    cpu.step(mem);
    CHECK(cpu.get_program_counter() == user_code_va + 4u, "User instruction executed");
    CHECK(cpu.read_register(1) == 11, "X1 incremented from 10 to 11");

    std::cout << "[PASS] test_el0_irq_exception_routing_and_return\n";
    return true;
}

static bool test_el1_irq_exception_routing_and_return() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0xFFFFFFFFu, 4);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // EL1 code at 0x2000: NOP (0xD503201Fu)
    const std::uint64_t el1_code_va = 0x2000ULL;
    mem.write_u32(static_cast<std::size_t>(el1_code_va), 0xD530CC00u); // MRS X0, ICC_IAR1_EL1 in handler

    // EL1 handler at vbar + 0x280 (Current EL SPx IRQ vector):
    // 0x280: MRS X0, ICC_IAR1_EL1 (0xD530CC00u)
    // 0x284: MSR ICC_EOIR1_EL1, X0 (0xD510CC20u)
    // 0x288: ERET (0xD69F03E0u)
    const std::uint64_t irq_handler_va = vbar + 0x280ULL;
    mem.write_u32(static_cast<std::size_t>(irq_handler_va), 0xD530CC00u);
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 4u), 0xD510CC20u);
    mem.write_u32(static_cast<std::size_t>(irq_handler_va + 8u), 0xD69F03E0u);

    // Enable SPI INTID 37, Priority 0x30
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 5u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 5u, 0x30u, 1);
    gic.set_spi_level(37, true);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_daif(0); // Interrupts enabled at EL1
    cpu.set_program_counter(el1_code_va);

    // Step 1: IRQ taken at EL1
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == irq_handler_va, "Must jump to Current EL SPx IRQ vector (VBAR + 0x280)");
    CHECK(cpu.get_elr_el1() == el1_code_va, "ELR_EL1 must hold interrupted EL1 PC");

    // Acknowledge, EOI, and ERET in handler
    cpu.step(mem); // MRS
    CHECK(cpu.read_register(0) == 37u, "X0 must hold acknowledged INTID 37");
    gic.set_spi_level(37, false);

    cpu.step(mem); // MSR EOIR
    cpu.step(mem); // ERET

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1 after return");
    CHECK(cpu.get_program_counter() == el1_code_va, "PC restored to el1_code_va");

    std::cout << "[PASS] test_el1_irq_exception_routing_and_return\n";
    return true;
}

static bool test_daif_i_masking_prevents_delivery() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x3u, 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0084u, 0xFFFFFFFFu, 4);
    gic.write_icc_igrpen1(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // User code at 0x1000: ADD X1, X1, #1 (0x91000421u)
    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_va), 0x91000421u);

    // Enable SPI INTID 38
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 6u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 6u, 0x20u, 1);
    gic.set_spi_level(38, true);

    // Set CPU to EL0, but MASK IRQ via DAIF bit 1 (0x2u)
    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0x2u); // I-bit masked
    cpu.set_program_counter(user_code_va);
    cpu.write_register(1, 5);

    // Step 1: Because DAIF.I is masked, interrupt must NOT be taken! Instruction executes!
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 0, "Must remain at EL0 while masked");
    CHECK(cpu.get_program_counter() == user_code_va + 4u, "PC must advance past ADD");
    CHECK(cpu.read_register(1) == 6, "ADD executed (X1 = 6)");

    // Now unmask IRQ (DAIF = 0)
    cpu.set_daif(0);

    // Step 2: Now interrupt must be delivered immediately!
    cpu.step(mem);
    CHECK(cpu.get_current_el() == 1, "Must enter EL1 when unmasked");
    CHECK(cpu.get_program_counter() == vbar + 0x480ULL, "Must jump to Lower EL IRQ vector");

    gic.set_spi_level(38, false);
    std::cout << "[PASS] test_daif_i_masking_prevents_delivery\n";
    return true;
}

static bool test_fiq_exception_routing() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x400000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    GICv3& gic = cpu.get_gic();
    gic.reset();
    gic.set_core_count(1);

    // Enable Group 0 (FIQ) in Distributor and CPU interface
    gic.write_mmio(GICv3::kGicdBase + 0x0000u, 0x1u, 4); // GICD_CTLR.EnableGrp0
    // GICD_IGROUPR remains 0 (Group 0 / FIQ)
    gic.write_icc_igrpen0(1, 0);
    gic.write_icc_pmr(0xF0u, 0);

    // Enable SPI INTID 39 (Group 0)
    gic.write_mmio(GICv3::kGicdBase + 0x0104u, (1u << 7u), 4);
    gic.write_mmio(GICv3::kGicdBase + 0x0420u + 7u, 0x10u, 1);
    gic.set_spi_level(39, true);

    // FIQ handler at vbar + 0x500 (Lower EL AArch64 FIQ vector):
    // 0x500: MRS X0, ICC_IAR0_EL1 (0xD538C800u)
    // 0x504: MSR ICC_EOIR0_EL1, X0 (0xD518C820u)
    // 0x508: ERET (0xD69F03E0u)
    const std::uint64_t fiq_handler_va = vbar + 0x500ULL;
    mem.write_u32(static_cast<std::size_t>(fiq_handler_va), 0xD538C800u);
    mem.write_u32(static_cast<std::size_t>(fiq_handler_va + 4u), 0xD518C820u);
    mem.write_u32(static_cast<std::size_t>(fiq_handler_va + 8u), 0xD69F03E0u);

    const std::uint64_t user_code_va = 0x1000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code_va), 0xD503201Fu); // NOP

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_daif(0); // FIQ and IRQ unmasked
    cpu.set_program_counter(user_code_va);

    // Step 1: FIQ taken from EL0
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must enter EL1 on FIQ");
    CHECK(cpu.get_program_counter() == fiq_handler_va, "Must jump to Lower EL FIQ vector (VBAR + 0x500)");
    CHECK(cpu.get_elr_el1() == user_code_va, "ELR_EL1 must hold interrupted PC");

    // In handler: MRS X0, ICC_IAR0_EL1
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 39u, "X0 must hold acknowledged Group 0 INTID 39");

    gic.set_spi_level(39, false);
    cpu.step(mem); // MSR EOIR0
    cpu.step(mem); // ERET

    CHECK(cpu.get_current_el() == 0, "Returned to EL0");
    CHECK(cpu.get_program_counter() == user_code_va, "Resumed user PC");

    std::cout << "[PASS] test_fiq_exception_routing\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.13: IRQ/FIQ Exception Routing Tests ===\n";
        if (!test_el0_irq_exception_routing_and_return()) return 1;
        if (!test_el1_irq_exception_routing_and_return()) return 1;
        if (!test_daif_i_masking_prevents_delivery()) return 1;
        if (!test_fiq_exception_routing()) return 1;
        std::cout << "=== All Stage 193.13 Tests Passed (4/4) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
