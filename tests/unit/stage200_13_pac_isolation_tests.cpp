// ==============================================================================
// WinIOS Campaign 200 Stage 13: PAC Key & Context Isolation Tests
// ==============================================================================
// Verifies:
// 1. APIA, APIB, APDA, APDB, APGA key registration and system register decoding
// 2. Minimum execution level requirement (min_el == 1) for all PAC key registers
// 3. EL0 MSR attempts raise privilege traps / exceptions and leave keys unmodified
// 4. EL1 MSR / MRS configuration of PAC keys
// 5. Cross-process / thread context switching PAC isolation:
//    - Process A signs pointer with Keyring A
//    - Context switch to Process B with Keyring B
//    - Authentication of Process A pointer under Keyring B fails
//    - Context switch back to Process A with Keyring A restored
//    - Authentication succeeds
// ==============================================================================

#include <cassert>
#include <cstdint>
#include <iostream>

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

using namespace winios::sysreg;
using namespace winios::cpu;

namespace {

std::uint32_t encode_msr(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5100000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

} // namespace

static void test_pac_sysreg_privilege_metadata() {
    const char* const key_names[] = {
        "APIAKeyLo_EL1", "APIAKeyHi_EL1",
        "APIBKeyLo_EL1", "APIBKeyHi_EL1",
        "APDAKeyLo_EL1", "APDAKeyHi_EL1",
        "APDBKeyLo_EL1", "APDBKeyHi_EL1",
        "APGAKeyLo_EL1", "APGAKeyHi_EL1"
    };

    for (const char* name : key_names) {
        const auto* desc = find_descriptor_by_name(name);
        assert(desc != nullptr);
        assert(desc->min_el == 1); // EL1 or higher only!
        assert(desc->is_readable);
        assert(desc->is_writable);
        (void)desc;
    }

    std::cout << "[PASS] test_pac_sysreg_privilege_metadata\n";
}

static void test_el0_privilege_trap_on_pac_keys() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    Memory mem(0x20000);

    const auto init_apia_lo = cpu.get_pac_keyring().apia.lo;

    // Run in EL0
    cpu.set_current_el(0);
    cpu.write_register(0, 0xCAFEBABE11223344ULL);

    // MSR APIAKeyLo_EL1, X0 (op0=3, op1=0, crn=2, crm=1, op2=0, rt=0)
    const std::uint64_t code_addr = 0x1000;
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_msr(3, 0, 2, 1, 0, 0));
    cpu.set_program_counter(code_addr);

    cpu.step(mem);

    // Must have taken an architectural exception / privilege trap
    assert(cpu.exception_taken_in_step());
    // Key must not have been modified!
    assert(cpu.get_pac_keyring().apia.lo == init_apia_lo);
    assert(cpu.get_pac_keyring().apia.lo != 0xCAFEBABE11223344ULL);
    (void)init_apia_lo;

    std::cout << "[PASS] test_el0_privilege_trap_on_pac_keys\n";
}

static void test_el1_pac_key_programming() {
    CPU cpu;
    cpu.reset();
    Memory mem(0x20000);

    // Set to EL1
    cpu.set_current_el(1);
    const std::uint64_t new_key_lo = 0x1122334455667788ULL;
    const std::uint64_t new_key_hi = 0x99AABBCCDDEEFF00ULL;

    cpu.write_register(1, new_key_lo);
    cpu.write_register(2, new_key_hi);

    const std::uint64_t code_addr = 0x2000;
    // MSR APIAKeyLo_EL1, X1
    mem.write_u32(static_cast<std::size_t>(code_addr), encode_msr(3, 0, 2, 1, 0, 1));
    // MSR APIAKeyHi_EL1, X2
    mem.write_u32(static_cast<std::size_t>(code_addr + 4), encode_msr(3, 0, 2, 1, 1, 2));

    cpu.set_program_counter(code_addr);

    cpu.step(mem);
    assert(!cpu.exception_taken_in_step());
    assert(cpu.get_pac_keyring().apia.lo == new_key_lo);

    cpu.step(mem);
    assert(!cpu.exception_taken_in_step());
    assert(cpu.get_pac_keyring().apia.hi == new_key_hi);

    std::cout << "[PASS] test_el1_pac_key_programming\n";
}

static void test_process_context_switch_pac_isolation() {
    Arm64ePointerAuth pac_procA;
    Arm64ePointerAuth pac_procB;

    // Proc A keys
    pac_procA.set_key(PacKeyType::InstructionA, 0x1111111111111111ULL, 0x2222222222222222ULL);

    // Proc B keys
    pac_procB.set_key(PacKeyType::InstructionA, 0xAAAAAAAAAAAAAAAAULL, 0xBBBBBBBBBBBBBBBBULL);

    const std::uint64_t ptr = 0x0000000100004000ULL;
    const std::uint64_t mod = 0x55AA55AAULL;

    // Process A signs pointer
    const std::uint64_t signed_ptr_A = pac_procA.sign_pointer(ptr, mod, PacKeyType::InstructionA);
    assert(signed_ptr_A != ptr);

    // Authenticating under Process A succeeds
    const std::uint64_t auth_A = pac_procA.authenticate_pointer(signed_ptr_A, mod, PacKeyType::InstructionA);
    assert(auth_A == ptr);
    assert(!pac_procA.has_fault());
    (void)auth_A;

    // Context switch to Process B: authenticating Process A's signed pointer FAILS
    const std::uint64_t auth_B = pac_procB.authenticate_pointer(signed_ptr_A, mod, PacKeyType::InstructionA);
    assert(pac_procB.has_fault());
    (void)auth_B;

    // Context switch back to Process A: authenticating succeeds again
    pac_procA.clear_fault();
    const std::uint64_t auth_A_restored = pac_procA.authenticate_pointer(signed_ptr_A, mod, PacKeyType::InstructionA);
    assert(auth_A_restored == ptr);
    assert(!pac_procA.has_fault());
    (void)auth_A_restored;

    std::cout << "[PASS] test_process_context_switch_pac_isolation\n";
}

int main() {
    std::cerr << "=== Running Stage 200.13 PAC Key & Context Isolation Tests ===" << std::endl;
    std::cerr << "Running test 1..." << std::endl;
    test_pac_sysreg_privilege_metadata();
    std::cerr << "Running test 2..." << std::endl;
    test_el0_privilege_trap_on_pac_keys();
    std::cerr << "Running test 3..." << std::endl;
    test_el1_pac_key_programming();
    std::cerr << "Running test 4..." << std::endl;
    test_process_context_switch_pac_isolation();
    std::cerr << "All Stage 200.13 tests passed successfully!" << std::endl;
    return 0;
}
