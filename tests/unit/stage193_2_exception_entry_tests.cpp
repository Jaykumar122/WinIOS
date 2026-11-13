// =============================================================================
// stage193_2_exception_entry_tests.cpp
//
// Stage 193.2: Centralized Guest Exception Entry Architecture Tests
// Verifies:
//   - Centralized delivery via deliver_architectural_exception.
//   - Synchronous exception entry from EL0 (SVC):
//       * SPSR_EL1 captures EL0 PSTATE (M=0x00).
//       * ELR_EL1 captures preferred return address.
//       * ESR_EL1 captures EC=0x15 and ISS.
//       * Vector target = VBAR_EL1 + 0x400 (Lower EL AArch64 Sync).
//       * DAIF masked to 0xF, SPSel set to 1.
//   - Data Abort exception entry from EL0:
//       * FAR_EL1 captures data fault address.
//       * ESR_EL1 captures EC=0x24 (Data Abort Lower) and WnR/DFSC.
//       * Vector target = VBAR_EL1 + 0x400.
//   - Instruction Abort exception entry from EL0:
//       * FAR_EL1 and ELR_EL1 capture instruction fetch fault address.
//       * ESR_EL1 captures EC=0x20 (Instruction Abort Lower).
//       * Vector target = VBAR_EL1 + 0x400.
//   - Asynchronous IRQ exception entry from EL0:
//       * Vector target = VBAR_EL1 + 0x480 (Lower EL AArch64 IRQ).
//       * ELR_EL1 captures interrupted PC.
//   - Asynchronous FIQ exception entry from EL0:
//       * Vector target = VBAR_EL1 + 0x500 (Lower EL AArch64 FIQ).
//   - Synchronous exception entry from EL1 with SPx (EL1h):
//       * SPSR_EL1 captures EL1h PSTATE (M=0x05).
//       * Vector target = VBAR_EL1 + 0x200 (Current EL SPx Sync).
// =============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/synchronous_exception.h"
#include "memory/memory.h"

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Test 1: Synchronous SVC Exception Entry from EL0
bool test_svc_entry_from_el0() {
    CPU cpu;
    Memory mem(0x100000ULL);
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbar = 0x80000ULL;
    constexpr std::uint64_t kCode = 0x10000ULL;
    cpu.set_vbar_el1(kVbar);

    // SVC #0x42: 1101 0100 0000 0000 0000 1000 0100 0001 = 0xD4000841u
    mem.write_u32(static_cast<std::size_t>(kCode), 0xD4000841u);

    cpu.set_current_el(0);
    cpu.set_program_counter(kCode);
    cpu.set_nzcv(0b1010); // N=1, Z=0, C=1, V=0
    cpu.set_daif(0b0011); // I=1, F=1

    cpu.step(mem);

    // Verify CPU state after SVC delivery
    CHECK(cpu.get_current_el() == 1, "Exception must transition to EL1");
    CHECK(cpu.get_sp_sel() == true, "SPSel must be true (EL1h)");
    CHECK(cpu.get_daif() == 0xFu, "DAIF must be fully masked (0xF) on exception entry");
    CHECK(cpu.get_program_counter() == kVbar + 0x400u, "PC must jump to Lower EL Sync vector (VBAR + 0x400)");
    CHECK(cpu.get_elr_el1() == kCode + 4u, "ELR_EL1 must hold return address (PC + 4 for SVC)");

    const auto spsr = cpu.get_spsr_el1();
    CHECK((spsr & 0x1Fu) == 0x00u, "SPSR_EL1.M must be 0x00 (EL0t)");
    CHECK(((spsr >> 28u) & 0xFu) == 0b1010u, "SPSR_EL1.NZCV must preserve previous condition flags");
    CHECK(((spsr >> 6u) & 0xFu) == 0b0011u, "SPSR_EL1.DAIF must preserve previous interrupt masks");

    const auto esr = cpu.get_esr_el1();
    const auto ec = (esr >> 26u) & 0x3Fu;
    const auto iss = esr & 0xFFFFu;
    CHECK(ec == 0x15u, "ESR_EL1.EC must be 0x15 (SVC in AArch64)");
    CHECK(iss == 0x42u, "ESR_EL1.ISS must capture SVC immediate 0x42");

    std::cout << "[PASS] test_svc_entry_from_el0\n";
    return true;
}

// Test 2: Data Abort Entry from EL0 with Fault Address
bool test_data_abort_entry_from_el0() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbar = 0x90000ULL;
    constexpr std::uint64_t kFaultPc = 0x24000ULL;
    constexpr std::uint64_t kFaultAddr = 0xDEADBEEF000ULL;
    cpu.set_vbar_el1(kVbar);
    cpu.set_current_el(0);
    cpu.set_program_counter(kFaultPc);

    // Generate Data Abort (Lower EL, Write, PermissionLevel3, 4 bytes)
    const SynchronousException ex = make_data_abort(
        false, AccessType::Write, FaultStatus::PermissionLevel3,
        kFaultAddr, kFaultPc, 4u);

    cpu.raise_synchronous_exception(ex);

    CHECK(cpu.get_current_el() == 1, "Data abort must enter EL1");
    CHECK(cpu.get_program_counter() == kVbar + 0x400u, "PC must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == kFaultPc, "ELR_EL1 must point to faulting instruction");
    CHECK(cpu.get_far_el1() == kFaultAddr, "FAR_EL1 must hold exact fault address");

    const auto esr = cpu.get_esr_el1();
    const auto ec = (esr >> 26u) & 0x3Fu;
    const auto wnr = (esr >> 6u) & 1u;
    const auto dfsc = esr & 0x3Fu;
    CHECK(ec == 0x24u, "ESR_EL1.EC must be 0x24 (Data Abort Lower)");
    CHECK(wnr == 1u, "ESR_EL1.WnR must indicate Write (1)");
    CHECK(dfsc == static_cast<std::uint32_t>(FaultStatus::PermissionLevel3), "ESR_EL1.DFSC must be PermissionLevel3");

    std::cout << "[PASS] test_data_abort_entry_from_el0\n";
    return true;
}

// Test 3: Instruction Abort Entry from EL0
bool test_instruction_abort_entry_from_el0() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbar = 0x70000ULL;
    constexpr std::uint64_t kFetchAddr = 0xB0000ULL;
    cpu.set_vbar_el1(kVbar);
    cpu.set_current_el(0);
    cpu.set_program_counter(kFetchAddr);

    // Generate Instruction Abort (Lower EL, TranslationLevel2)
    const SynchronousException ex = make_instruction_abort(
        false, FaultStatus::TranslationLevel2, kFetchAddr, 4u);

    cpu.raise_synchronous_exception(ex);

    CHECK(cpu.get_current_el() == 1, "Instruction abort must enter EL1");
    CHECK(cpu.get_program_counter() == kVbar + 0x400u, "PC must jump to Lower EL Sync vector");
    CHECK(cpu.get_elr_el1() == kFetchAddr, "ELR_EL1 must hold faulting fetch address");
    CHECK(cpu.get_far_el1() == kFetchAddr, "FAR_EL1 must hold faulting fetch address");

    const auto esr = cpu.get_esr_el1();
    const auto ec = (esr >> 26u) & 0x3Fu;
    const auto ifsc = esr & 0x3Fu;
    CHECK(ec == 0x20u, "ESR_EL1.EC must be 0x20 (Instruction Abort Lower)");
    CHECK(ifsc == static_cast<std::uint32_t>(FaultStatus::TranslationLevel2), "ESR_EL1.IFSC must be TranslationLevel2");

    std::cout << "[PASS] test_instruction_abort_entry_from_el0\n";
    return true;
}

// Test 4: Asynchronous IRQ & FIQ Entry from EL0
bool test_async_interrupt_entry_from_el0() {
    CPU cpu;
    constexpr std::uint64_t kVbar = 0x60000ULL;
    constexpr std::uint64_t kUserPc = 0x12000ULL;
    cpu.set_vbar_el1(kVbar);

    // Test IRQ entry
    cpu.set_current_el(0);
    cpu.set_program_counter(kUserPc);
    cpu.set_daif(0b0000); // Interrupts unmasked

    cpu.raise_asynchronous_exception(false, 30u, false, 0x80u);

    CHECK(cpu.get_current_el() == 1, "IRQ must enter EL1");
    CHECK(cpu.get_program_counter() == kVbar + 0x480u, "PC must jump to Lower EL IRQ vector (VBAR + 0x480)");
    CHECK(cpu.get_elr_el1() == kUserPc, "ELR_EL1 must capture interrupted PC");
    CHECK(cpu.get_daif() == 0xFu, "DAIF must be masked on IRQ entry");
    CHECK((cpu.get_spsr_el1() & 0x1Fu) == 0x00u, "SPSR_EL1.M must be EL0t");

    // Test FIQ entry
    cpu.set_current_el(0);
    cpu.set_program_counter(kUserPc + 8u);
    cpu.set_daif(0b0000);

    cpu.raise_asynchronous_exception(true, 27u, false, 0x80u);

    CHECK(cpu.get_current_el() == 1, "FIQ must enter EL1");
    CHECK(cpu.get_program_counter() == kVbar + 0x500u, "PC must jump to Lower EL FIQ vector (VBAR + 0x500)");
    CHECK(cpu.get_elr_el1() == kUserPc + 8u, "ELR_EL1 must capture interrupted PC");
    CHECK((cpu.get_spsr_el1() & 0x1Fu) == 0x00u, "SPSR_EL1.M must be EL0t");

    std::cout << "[PASS] test_async_interrupt_entry_from_el0\n";
    return true;
}

// Test 5: Synchronous Exception Entry from EL1 with SPx (EL1h)
bool test_sync_entry_from_el1h() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    constexpr std::uint64_t kVbar = 0x50000ULL;
    constexpr std::uint64_t kKernelPc = 0x30000ULL;
    cpu.set_vbar_el1(kVbar);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true); // EL1h
    cpu.set_program_counter(kKernelPc);
    cpu.set_nzcv(0b0100); // Z=1

    // Generate Undefined Instruction at EL1
    const SynchronousException ex = make_undefined_instruction(kKernelPc);
    cpu.raise_synchronous_exception(ex);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_sp_sel() == true, "Must remain in EL1h");
    CHECK(cpu.get_program_counter() == kVbar + 0x200u, "PC must jump to Current EL SPx Sync vector (VBAR + 0x200)");
    CHECK(cpu.get_elr_el1() == kKernelPc, "ELR_EL1 must hold faulting instruction PC");

    const auto spsr = cpu.get_spsr_el1();
    CHECK((spsr & 0x1Fu) == 0x05u, "SPSR_EL1.M must be 0x05 (EL1h)");
    CHECK(((spsr >> 28u) & 0xFu) == 0b0100u, "SPSR_EL1.NZCV must preserve Z=1");

    const auto esr = cpu.get_esr_el1();
    CHECK(((esr >> 26u) & 0x3Fu) == 0x00u, "ESR_EL1.EC must be 0x00 (Undefined Instruction)");

    std::cout << "[PASS] test_sync_entry_from_el1h\n";
    return true;
}

} // namespace

int main() {
    std::cout << "=== Running Stage 193.2: Exception Entry Architecture Tests ===\n";

    if (!test_svc_entry_from_el0()) return 1;
    if (!test_data_abort_entry_from_el0()) return 1;
    if (!test_instruction_abort_entry_from_el0()) return 1;
    if (!test_async_interrupt_entry_from_el0()) return 1;
    if (!test_sync_entry_from_el1h()) return 1;

    std::cout << "=== All Stage 193.2 Tests Passed (5/5) ===\n";
    return 0;
}
