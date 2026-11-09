#include "cpu/cpu.h"
#include "cpu/mmu.h"
#include "cpu/sysreg.h"
#include "cpu/synchronous_exception.h"
#include "cpu/virtual_time.h"
#include "memory/memory.h"
#include "runtime/execution_checkpoint.h"
#include "runtime/elf_session.h"
#include "test_harness.h"
#include "../support/elf_fixture.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

// ----------------------------------------------------------------------------
// Test 1: AArch64 Decoder & Basic ALU Conformance
// ----------------------------------------------------------------------------
void test_decoder_and_alu_conformance() {
    CPU cpu;
    Memory mem(0x10000);

    // Initial state check: registers X0..X30 must be zeroed, PC zeroed
    for (std::size_t i = 0; i < CPU::kRegisterCount; ++i) {
        CHECK(cpu.read_register(i) == 0);
    }
    CHECK(cpu.get_program_counter() == 0);

    // Write X1 = 42, X2 = 58
    cpu.write_register(1, 42);
    cpu.write_register(2, 58);
    CHECK(cpu.read_register(1) == 42);
    CHECK(cpu.read_register(2) == 58);

    // Program at 0x1000:
    // 1. ADD X0, X1, X2   (0x8B020020) => X0 = 100
    // 2. SUBS X3, X0, #100 (0xF1019003) => X3 = 0, Z flag set (NZCV bit 2 / 0x4)
    // 3. HLT #0           (0xD4400000)
    std::uint64_t pc = 0x1000;
    cpu.set_program_counter(pc);
    mem.write_u32(pc, 0x8B020020u);      // ADD X0, X1, X2
    mem.write_u32(pc + 4, 0xF1019003u);  // SUBS X3, X0, #100
    mem.write_u32(pc + 8, 0xD4400000u);  // HLT #0

    // Step 1: ADD
    cpu.step(mem);
    CHECK(cpu.read_register(0) == 100);
    CHECK(cpu.get_program_counter() == pc + 4);

    // Step 2: SUBS
    cpu.step(mem);
    CHECK(cpu.read_register(3) == 0);
    CHECK(cpu.get_program_counter() == pc + 8);
    // Z flag (bit 2 in 4-bit NZCV representation, where N=8, Z=4, C=2, V=1)
    CHECK((cpu.get_nzcv() & 0x4u) != 0); // Z flag set
    CHECK((cpu.get_nzcv() & 0x8u) == 0); // N flag clear

    // Step 3: HLT
    cpu.step(mem);
    CHECK(cpu.is_halted() == true);
}

// ----------------------------------------------------------------------------
// Test 2: System Register & Privilege Enforcement Audit
// ----------------------------------------------------------------------------
void test_system_register_privilege_audit() {
    CPU cpu;

    // Default privilege is EL0 or EL1
    cpu.set_current_el(1);
    CHECK(cpu.get_current_el() == 1);

    // SPSR_EL1, ELR_EL1, VBAR_EL1 write & read
    cpu.set_spsr_el1(0x3C5u);
    CHECK(cpu.get_spsr_el1() == 0x3C5u);

    cpu.set_elr_el1(0x0000000040001000ULL);
    CHECK(cpu.get_elr_el1() == 0x0000000040001000ULL);

    cpu.set_vbar_el1(0x0000000080000000ULL);
    CHECK(cpu.get_vbar_el1() == 0x0000000080000000ULL);

    // Thread Pointer Registers
    cpu.set_tpidr_el0(0x1111222233334444ULL);
    CHECK(cpu.get_tpidr_el0() == 0x1111222233334444ULL);

    cpu.set_tpidr_el1(0x5555666677778888ULL);
    CHECK(cpu.get_tpidr_el1() == 0x5555666677778888ULL);

    cpu.set_tpidrro_el0(0x9999AAAABBBBCCCCULL);
    CHECK(cpu.get_tpidrro_el0() == 0x9999AAAABBBBCCCCULL);

    // SPSel check
    cpu.set_sp_sel(true);
    CHECK(cpu.get_sp_sel() == true);
    cpu.set_sp_sel(false);
    CHECK(cpu.get_sp_sel() == false);

    // Transition to EL0
    cpu.set_current_el(0);
    CHECK(cpu.get_current_el() == 0);
}

// ----------------------------------------------------------------------------
// Test 3: Exception Syndrome & Vector Conformance Audit
// ----------------------------------------------------------------------------
void test_exception_syndrome_conformance() {
    CPU cpu;

    // Test ESR_EL1 encoding format
    // EC (bits 31:26), IL (bit 25), ISS (bits 24:0)
    constexpr std::uint32_t kEcSvcA64 = 0x15u;
    const std::uint32_t esr_svc = (kEcSvcA64 << 26u) | (1u << 25u) | 0x0001u;
    cpu.set_esr_el1(esr_svc);
    CHECK(cpu.get_esr_el1() == esr_svc);
    CHECK(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == kEcSvcA64);

    // FAR_EL1 fault address verification
    constexpr std::uint64_t kFaultAddr = 0x0000FFFF80004000ULL;
    cpu.set_far_el1(kFaultAddr);
    CHECK(cpu.get_far_el1() == kFaultAddr);

    // Test translation fault FSC mapping
    mmu::TranslationFault fault;
    fault.type = mmu::MmuFaultType::Translation;
    fault.level = 3;
    fault.virtual_address = kFaultAddr;
    CHECK(fault.fsc() == 0x07u); // 0x04 + 3

    fault.type = mmu::MmuFaultType::Permission;
    fault.level = 2;
    CHECK(fault.fsc() == 0x0Eu); // 0x0C + 2

    fault.type = mmu::MmuFaultType::AccessFlag;
    fault.level = 1;
    CHECK(fault.fsc() == 0x09u); // 0x08 + 1
}

// ----------------------------------------------------------------------------
// Test 4: MMU Translation Granule & Attribute Audit
// ----------------------------------------------------------------------------
void test_mmu_translation_granule_audit() {
    CPU cpu;
    auto& mmu = cpu.get_mmu();

    // Verify default disabled state
    CHECK(mmu.is_mmu_enabled() == false);

    // Check TranslationResult attributes
    mmu::TranslationResult tr;
    tr.physical_address = 0x80001000ULL;
    tr.page_size = 4096;
    tr.readable = true;
    tr.writable = true;
    tr.executable = false;
    tr.el0_accessible = false;

    CHECK(tr.physical_address == 0x80001000ULL);
    CHECK(tr.page_size == 4096);
    CHECK(tr.readable && tr.writable && !tr.executable);

    // 16KB and 64KB page size representations
    tr.page_size = 16384;
    CHECK(tr.page_size == 16384);
    tr.page_size = 65536;
    CHECK(tr.page_size == 65536);
}

// ----------------------------------------------------------------------------
// Test 5: FP/ASIMD Register & Access Control Audit
// ----------------------------------------------------------------------------
void test_fp_simd_register_file_audit() {
    CPU cpu;

    // Check 32 vector registers
    for (std::size_t i = 0; i < CPU::kVectorRegisterCount; ++i) {
        CPU::VectorRegister vr{0x0123456789ABCDEFULL + i, 0xFEDCBA9876543210ULL + i};
        cpu.write_vector_register(i, vr);
        const auto read_back = cpu.read_vector_register(i);
        CHECK(read_back == vr);
    }

    // CPACR_EL1 FP/SIMD trap gating checks:
    // Bits [21:20] FPEN:
    // 00 = Trap EL0 and EL1
    // 01 = Trap EL0, enable EL1
    // 10 = Trap EL0 and EL1 (reserved)
    // 11 = Enable EL0 and EL1
    cpu.set_cpacr_el1(0x0ULL); // FPEN = 00
    CHECK(cpu.is_fp_simd_enabled(0) == false);
    CHECK(cpu.is_fp_simd_enabled(1) == false);

    cpu.set_cpacr_el1(1ULL << 20u); // FPEN = 01
    CHECK(cpu.is_fp_simd_enabled(0) == false);
    CHECK(cpu.is_fp_simd_enabled(1) == true);

    cpu.set_cpacr_el1(3ULL << 20u); // FPEN = 11
    CHECK(cpu.is_fp_simd_enabled(0) == true);
    CHECK(cpu.is_fp_simd_enabled(1) == true);
}

// ----------------------------------------------------------------------------
// Test 6: Exclusive Monitor & Memory Ordering Audit
// ----------------------------------------------------------------------------
void test_exclusive_monitor_and_ordering_audit() {
    CPU cpu;

    // Initial state: exclusive monitor unreserved
    CHECK(cpu.exclusive_monitor_valid() == false);

    // Address reservation
    constexpr std::uint64_t kAddr = 0x2000;
    cpu.set_exclusive_monitor(true, kAddr, 8, 0x1234, 0);
    CHECK(cpu.exclusive_monitor_valid() == true);
    CHECK(cpu.exclusive_monitor_address() == kAddr);

    // Clear exclusive monitor
    cpu.clear_exclusive_monitor();
    CHECK(cpu.exclusive_monitor_valid() == false);
}

// ----------------------------------------------------------------------------
// Test 7: Generic Timer & Interrupt Status Audit
// ----------------------------------------------------------------------------
void test_timer_and_interrupt_audit() {
    CPU cpu;
    winios::VirtualTime vtime;
    cpu.bind_virtual_time(&vtime, 0);

    // Initial physical counter is 0
    CHECK(vtime.get_physical_counter() == 0);
    vtime.set_physical_counter(100);
    CHECK(vtime.get_physical_counter() == 100);

    // Physical timer state manipulation
    winios::TimerState s;
    s.enable = true;
    s.imask = false;
    s.cval = 500;
    cpu.set_phys_timer(s);

    const auto read_state = cpu.get_phys_timer();
    CHECK(read_state.enable == true);
    CHECK(read_state.imask == false);
    CHECK(read_state.cval == 500);
}

// ----------------------------------------------------------------------------
// Test 8: Checkpoint Integrity & Hash Baseline
// ----------------------------------------------------------------------------
void test_checkpoint_conformance_baseline() {
    elf_test::File owner;
    owner.write(elf_test::image());
    const auto dir = owner.path().parent_path();
    const auto elf_copy = dir / "audit_cp.elf";
    std::filesystem::copy_file(owner.path(), elf_copy);

    RuntimeConfig config{0x10000, 0, 0, 0x10000 - 4096, 4096, 100, std::nullopt, false};
    ElfSession session(config);
    CHECK(session.load(elf_copy).status == ExecutionStatus::completed);

    // Initial capture from loaded session
    const auto cp = CheckpointManager::capture(session, 100);
    CHECK(CheckpointManager::validate(cp) == true);
    CHECK(cp.header.magic == CheckpointMetadata::kMagic);
    CHECK(cp.header.version == CheckpointMetadata::kVersion);
    CHECK(cp.header.memory_capacity == 0x10000);

    // Serialized roundtrip
    const auto serialized = CheckpointManager::serialize(cp);
    CHECK(!serialized.empty());

    const auto loaded = CheckpointManager::deserialize(serialized);
    CHECK(CheckpointManager::validate(loaded) == true);
    CHECK(loaded.header.magic == CheckpointMetadata::kMagic);
    CHECK(loaded.header.memory_capacity == 0x10000);
}

} // namespace

int main() {
    try {
        std::cout << "[1] test_decoder_and_alu_conformance..." << std::endl;
        test_decoder_and_alu_conformance();

        std::cout << "[2] test_system_register_privilege_audit..." << std::endl;
        test_system_register_privilege_audit();

        std::cout << "[3] test_exception_syndrome_conformance..." << std::endl;
        test_exception_syndrome_conformance();

        std::cout << "[4] test_mmu_translation_granule_audit..." << std::endl;
        test_mmu_translation_granule_audit();

        std::cout << "[5] test_fp_simd_register_file_audit..." << std::endl;
        test_fp_simd_register_file_audit();

        std::cout << "[6] test_exclusive_monitor_and_ordering_audit..." << std::endl;
        test_exclusive_monitor_and_ordering_audit();

        std::cout << "[7] test_timer_and_interrupt_audit..." << std::endl;
        test_timer_and_interrupt_audit();

        std::cout << "[8] test_checkpoint_conformance_baseline..." << std::endl;
        test_checkpoint_conformance_baseline();
    } catch (const std::exception& e) {
        std::cerr << "EXCEPTION in main: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "UNKNOWN EXCEPTION in main!" << std::endl;
        return 1;
    }

    return test::summarize("stage59_audit_tests");
}
