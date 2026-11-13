// ============================================================================
// Stage 193.4: Vector Table Architecture Tests
// ============================================================================

#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
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

static bool test_vbar_alignment_masking() {
    CPU cpu;
    Memory mem(0x200000ULL);

    // 1. Direct API masking on VBAR_EL1
    cpu.set_vbar_el1(0x800007FFULL);
    CHECK(cpu.get_vbar_el1() == 0x80000000ULL, "VBAR_EL1 must mask lower 11 bits");

    // 2. Direct API masking on VBAR_EL2 and VBAR_EL3
    cpu.set_vbar_el2(0x900007FFULL);
    CHECK(cpu.get_vbar_el2() == 0x90000000ULL, "VBAR_EL2 must mask lower 11 bits");
    cpu.set_vbar_el3(0xA00007FFULL);
    CHECK(cpu.get_vbar_el3() == 0xA0000000ULL, "VBAR_EL3 must mask lower 11 bits");

    // 3. Guest MSR VBAR_EL1 instruction masking
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.write_register(0, 0xFFFF000012345678ULL);

    // MSR VBAR_EL1, X0 (0xD518C000u)
    mem.write_u32(0x1000, 0xD518C000u);
    // MRS X1, VBAR_EL1 (0xD538C001u)
    mem.write_u32(0x1004, 0xD538C001u);

    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    CHECK(cpu.get_vbar_el1() == 0xFFFF000012345000ULL, "Guest MSR VBAR_EL1 must mask lower 11 bits");

    cpu.step(mem);
    CHECK(cpu.read_register(1) == 0xFFFF000012345000ULL, "Guest MRS VBAR_EL1 must read masked value");

    std::cout << "[PASS] test_vbar_alignment_masking\n";
    return true;
}

static bool test_vector_offset_computation_all_groups() {
    CPU cpu;
    // Verify AArch32 lower EL support rejection
    CHECK(!cpu.supports_aarch32_lower_el(), "AArch32 lower EL must not be supported on A16 target");

    // Group 1: Current EL with SP0 (table_section = 0x000)
    CHECK(compute_vector_offset(1, 1, false, ExceptionType::Synchronous) == 0x000u, "Current EL SP0 Sync");
    CHECK(compute_vector_offset(1, 1, false, ExceptionType::IRQ)         == 0x080u, "Current EL SP0 IRQ");
    CHECK(compute_vector_offset(1, 1, false, ExceptionType::FIQ)         == 0x100u, "Current EL SP0 FIQ");
    CHECK(compute_vector_offset(1, 1, false, ExceptionType::SError)      == 0x180u, "Current EL SP0 SError");

    // Group 2: Current EL with SPx (table_section = 0x200)
    CHECK(compute_vector_offset(1, 1, true, ExceptionType::Synchronous)  == 0x200u, "Current EL SPx Sync");
    CHECK(compute_vector_offset(1, 1, true, ExceptionType::IRQ)          == 0x280u, "Current EL SPx IRQ");
    CHECK(compute_vector_offset(1, 1, true, ExceptionType::FIQ)          == 0x300u, "Current EL SPx FIQ");
    CHECK(compute_vector_offset(1, 1, true, ExceptionType::SError)       == 0x380u, "Current EL SPx SError");

    // Group 3: Lower EL using AArch64 (table_section = 0x400)
    CHECK(compute_vector_offset(0, 1, true,  ExceptionType::Synchronous) == 0x400u, "Lower EL AArch64 Sync");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::Synchronous) == 0x400u, "Lower EL AArch64 Sync");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::IRQ)         == 0x480u, "Lower EL AArch64 IRQ");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::FIQ)         == 0x500u, "Lower EL AArch64 FIQ");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::SError)      == 0x580u, "Lower EL AArch64 SError");

    // Group 4: Lower EL using AArch32 (table_section = 0x600)
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::Synchronous, true) == 0x600u, "Lower EL AArch32 Sync");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::IRQ,         true) == 0x680u, "Lower EL AArch32 IRQ");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::FIQ,         true) == 0x700u, "Lower EL AArch32 FIQ");
    CHECK(compute_vector_offset(0, 1, false, ExceptionType::SError,      true) == 0x780u, "Lower EL AArch32 SError");

    std::cout << "[PASS] test_vector_offset_computation_all_groups\n";
    return true;
}

static bool test_guest_instruction_exception_dispatch_lower_el() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // EL0 user code at 0x4000: SVC #0 (0xD4000001u)
    const std::uint64_t user_code = 0x4000ULL;
    mem.write_u32(static_cast<std::size_t>(user_code), 0xD4000001u);

    // Vector handler at vbar + 0x400 (Lower EL AArch64 Synchronous)
    // MOV X5, #100 (0xD2800C85u)
    const std::uint64_t handler_addr = vbar + 0x400ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr), 0xD2800C85u);

    cpu.set_current_el(0);
    cpu.set_program_counter(user_code);
    cpu.write_register(5, 0);

    // Step 1: Execute SVC #0 at EL0 -> causes exception to EL1 handler
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must enter EL1");
    CHECK(cpu.get_sp_sel() == true, "Must select SP_EL1");
    CHECK(cpu.get_program_counter() == handler_addr, "PC must be at vector handler");
    CHECK(cpu.get_elr_el1() == user_code + 4u, "ELR_EL1 must record preferred return PC (address + 4 for SVC)");
    CHECK(cpu.read_register(5) == 0, "Handler instruction not executed yet");

    // Step 2: Execute handler instruction at vbar + 0x400 (MOV X5, #100)
    cpu.step(mem);

    CHECK(cpu.read_register(5) == 100, "X5 must be updated by handler instruction");
    CHECK(cpu.get_program_counter() == handler_addr + 4, "PC must advance past handler instruction");

    std::cout << "[PASS] test_guest_instruction_exception_dispatch_lower_el\n";
    return true;
}

static bool test_guest_instruction_exception_dispatch_same_el_spx() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // EL1 code at 0x10000 with SPx: 0x00000000u (unallocated encoding) traps at EL1 as Undefined
    const std::uint64_t el1_code = 0x10000ULL;
    mem.write_u32(static_cast<std::size_t>(el1_code), 0x00000000u);

    // Vector handler at vbar + 0x200 (Current EL SPx Synchronous)
    // MOV X6, #200 (0xD2801906u)
    const std::uint64_t handler_addr = vbar + 0x200ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr), 0xD2801906u);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(el1_code);
    cpu.write_register(6, 0);

    // Step 1: Execute HVC #0 at EL1 with SPx -> takes exception to vbar + 0x200
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_sp_sel() == true, "Must have SPSel=1");
    CHECK(cpu.get_program_counter() == handler_addr, "PC must jump to vbar + 0x200");
    CHECK(cpu.get_elr_el1() == el1_code, "ELR_EL1 must record faulting PC");

    // Step 2: Execute handler instruction at vbar + 0x200 (MOV X6, #200)
    cpu.step(mem);

    CHECK(cpu.read_register(6) == 200, "X6 must be updated by vector handler");
    CHECK(cpu.get_program_counter() == handler_addr + 4, "PC must advance past vector instruction");

    std::cout << "[PASS] test_guest_instruction_exception_dispatch_same_el_spx\n";
    return true;
}

static bool test_guest_instruction_exception_dispatch_same_el_sp0() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // EL1 code at 0x10000 with SP0: 0x00000000u (unallocated encoding) traps at EL1 as Undefined
    const std::uint64_t el1_code = 0x10000ULL;
    mem.write_u32(static_cast<std::size_t>(el1_code), 0x00000000u);

    // Vector handler at vbar + 0x000 (Current EL SP0 Synchronous)
    // MOV X7, #300 (0xD2802587u)
    const std::uint64_t handler_addr = vbar + 0x000ULL;
    mem.write_u32(static_cast<std::size_t>(handler_addr), 0xD2802587u);

    cpu.set_current_el(1);
    cpu.set_sp_sel(false); // SP0
    cpu.set_program_counter(el1_code);
    cpu.write_register(7, 0);

    // Step 1: Execute HVC #0 at EL1 with SP0 -> takes exception to vbar + 0x000
    cpu.step(mem);

    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_sp_sel() == true, "ARMv8 takes exception with SPSel=1");
    CHECK(cpu.get_program_counter() == handler_addr, "PC must jump to vbar + 0x000");
    CHECK(cpu.get_elr_el1() == el1_code, "ELR_EL1 must record faulting PC");

    // Step 2: Execute handler instruction at vbar + 0x000 (MOV X7, #300)
    cpu.step(mem);

    CHECK(cpu.read_register(7) == 300, "X7 must be updated by vector handler");
    CHECK(cpu.get_program_counter() == handler_addr + 4, "PC must advance past vector instruction");

    std::cout << "[PASS] test_guest_instruction_exception_dispatch_same_el_sp0\n";
    return true;
}

static bool test_guest_asynchronous_vector_dispatch() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    // 1. Lower EL IRQ -> vector vbar + 0x480
    ArchitecturalException irq_lower{};
    irq_lower.type = ExceptionType::IRQ;
    irq_lower.target_el = 1;
    irq_lower.preferred_return = 0x5000ULL;
    cpu.set_current_el(0);
    cpu.deliver_architectural_exception(irq_lower);
    CHECK(cpu.get_program_counter() == vbar + 0x480ULL, "Lower EL IRQ vector offset");

    // 2. Lower EL FIQ -> vector vbar + 0x500
    ArchitecturalException fiq_lower{};
    fiq_lower.type = ExceptionType::FIQ;
    fiq_lower.target_el = 1;
    fiq_lower.preferred_return = 0x5000ULL;
    cpu.set_current_el(0);
    cpu.deliver_architectural_exception(fiq_lower);
    CHECK(cpu.get_program_counter() == vbar + 0x500ULL, "Lower EL FIQ vector offset");

    // 3. Current EL SPx IRQ -> vector vbar + 0x280
    ArchitecturalException irq_cur_spx{};
    irq_cur_spx.type = ExceptionType::IRQ;
    irq_cur_spx.target_el = 1;
    irq_cur_spx.preferred_return = 0x6000ULL;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.deliver_architectural_exception(irq_cur_spx);
    CHECK(cpu.get_program_counter() == vbar + 0x280ULL, "Current EL SPx IRQ vector offset");

    // 4. Current EL SPx FIQ -> vector vbar + 0x300
    ArchitecturalException fiq_cur_spx{};
    fiq_cur_spx.type = ExceptionType::FIQ;
    fiq_cur_spx.target_el = 1;
    fiq_cur_spx.preferred_return = 0x6000ULL;
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.deliver_architectural_exception(fiq_cur_spx);
    CHECK(cpu.get_program_counter() == vbar + 0x300ULL, "Current EL SPx FIQ vector offset");

    // 5. Current EL SP0 IRQ -> vector vbar + 0x080
    ArchitecturalException irq_cur_sp0{};
    irq_cur_sp0.type = ExceptionType::IRQ;
    irq_cur_sp0.target_el = 1;
    irq_cur_sp0.preferred_return = 0x7000ULL;
    cpu.set_current_el(1);
    cpu.set_sp_sel(false);
    cpu.deliver_architectural_exception(irq_cur_sp0);
    CHECK(cpu.get_program_counter() == vbar + 0x080ULL, "Current EL SP0 IRQ vector offset");

    // 6. Current EL SP0 FIQ -> vector vbar + 0x100
    ArchitecturalException fiq_cur_sp0{};
    fiq_cur_sp0.type = ExceptionType::FIQ;
    fiq_cur_sp0.target_el = 1;
    fiq_cur_sp0.preferred_return = 0x7000ULL;
    cpu.set_current_el(1);
    cpu.set_sp_sel(false);
    cpu.deliver_architectural_exception(fiq_cur_sp0);
    CHECK(cpu.get_program_counter() == vbar + 0x100ULL, "Current EL SP0 FIQ vector offset");

    std::cout << "[PASS] test_guest_asynchronous_vector_dispatch\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.4: Vector Table Architecture Tests ===\n";
        if (!test_vbar_alignment_masking()) return 1;
        if (!test_vector_offset_computation_all_groups()) return 1;
        if (!test_guest_instruction_exception_dispatch_lower_el()) return 1;
        if (!test_guest_instruction_exception_dispatch_same_el_spx()) return 1;
        if (!test_guest_instruction_exception_dispatch_same_el_sp0()) return 1;
        if (!test_guest_asynchronous_vector_dispatch()) return 1;
        std::cout << "=== All Stage 193.4 Tests Passed (6/6) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
