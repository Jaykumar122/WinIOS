#include <cassert>
#include <iostream>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
#include "memory/memory.h"

using namespace winios::sysreg;
using namespace winios::cpu;

namespace {

std::uint32_t encode_mrs(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5300000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

std::uint32_t encode_msr(std::uint8_t op0, std::uint8_t op1, std::uint8_t crn, std::uint8_t crm, std::uint8_t op2, std::uint32_t rt) {
    const std::uint32_t o0 = (op0 == 2) ? 0u : 1u;
    return 0xD5100000u | (o0 << 19u) | (static_cast<std::uint32_t>(op1 & 7u) << 16u) |
           (static_cast<std::uint32_t>(crn & 15u) << 12u) | (static_cast<std::uint32_t>(crm & 15u) << 8u) |
           (static_cast<std::uint32_t>(op2 & 7u) << 5u) | (rt & 31u);
}

} // namespace

void test_pac_sysreg_descriptors() {
    struct ExpectedReg {
        SysRegId id;
        const char* name;
        std::uint8_t op0, op1, crn, crm, op2;
    };

    const ExpectedReg expected[] = {
        { SysRegId::APIAKeyLo_EL1, "APIAKeyLo_EL1", 3, 0, 2, 1, 0 },
        { SysRegId::APIAKeyHi_EL1, "APIAKeyHi_EL1", 3, 0, 2, 1, 1 },
        { SysRegId::APIBKeyLo_EL1, "APIBKeyLo_EL1", 3, 0, 2, 1, 2 },
        { SysRegId::APIBKeyHi_EL1, "APIBKeyHi_EL1", 3, 0, 2, 1, 3 },
        { SysRegId::APDAKeyLo_EL1, "APDAKeyLo_EL1", 3, 0, 2, 2, 0 },
        { SysRegId::APDAKeyHi_EL1, "APDAKeyHi_EL1", 3, 0, 2, 2, 1 },
        { SysRegId::APDBKeyLo_EL1, "APDBKeyLo_EL1", 3, 0, 2, 2, 2 },
        { SysRegId::APDBKeyHi_EL1, "APDBKeyHi_EL1", 3, 0, 2, 2, 3 },
        { SysRegId::APGAKeyLo_EL1, "APGAKeyLo_EL1", 3, 0, 2, 3, 0 },
        { SysRegId::APGAKeyHi_EL1, "APGAKeyHi_EL1", 3, 0, 2, 3, 1 }
    };

    for (const auto& r : expected) {
        const auto* desc_name = find_descriptor_by_name(r.name);
        assert(desc_name != nullptr);
        assert(desc_name->id == r.id);
        assert(desc_name->op0 == r.op0);
        assert(desc_name->op1 == r.op1);
        assert(desc_name->crn == r.crn);
        assert(desc_name->crm == r.crm);
        assert(desc_name->op2 == r.op2);
        assert(desc_name->min_el == 1);
        assert(desc_name->is_readable);
        assert(desc_name->is_writable);
        assert(desc_name->width == 64);

        const std::uint16_t key = make_sysreg_key(r.op0, r.op1, r.crn, r.crm, r.op2);
        const auto* desc_key = find_descriptor_by_key(key);
        assert(desc_key == desc_name);
        (void)desc_name;
        (void)desc_key;
    }

    std::cout << "[PASS] test_pac_sysreg_descriptors" << std::endl;
}

void test_pac_privilege_checking() {
    CPU cpu;
    cpu.reset();

    const char* reg_names[] = {
        "APIAKeyLo_EL1", "APIAKeyHi_EL1",
        "APIBKeyLo_EL1", "APIBKeyHi_EL1",
        "APDAKeyLo_EL1", "APDAKeyHi_EL1",
        "APDBKeyLo_EL1", "APDBKeyHi_EL1",
        "APGAKeyLo_EL1", "APGAKeyHi_EL1"
    };

    for (const char* name : reg_names) {
        const auto* desc = find_descriptor_by_name(name);
        assert(desc != nullptr);

        // At EL0: reading and writing must result in UndefinedException
        assert(check_privilege(*desc, 0, true, cpu) == PrivilegeResult::UndefinedException);
        assert(check_privilege(*desc, 0, false, cpu) == PrivilegeResult::UndefinedException);

        // At EL1, EL2, EL3: access is Allowed
        assert(check_privilege(*desc, 1, true, cpu) == PrivilegeResult::Allowed);
        assert(check_privilege(*desc, 1, false, cpu) == PrivilegeResult::Allowed);

        assert(check_privilege(*desc, 2, true, cpu) == PrivilegeResult::Allowed);
        assert(check_privilege(*desc, 2, false, cpu) == PrivilegeResult::Allowed);

        assert(check_privilege(*desc, 3, true, cpu) == PrivilegeResult::Allowed);
        assert(check_privilege(*desc, 3, false, cpu) == PrivilegeResult::Allowed);
        (void)desc;
    }

    std::cout << "[PASS] test_pac_privilege_checking" << std::endl;
}

void test_pac_key_msr_mrs_roundtrip() {
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1); // Run at EL1
    Memory mem(0x10000);

    const std::uint64_t val_lo = 0x0123456789ABCDEFULL;
    const std::uint64_t val_hi = 0xFEDCBA9876543210ULL;

    // 1. Test APIAKey
    cpu.write_register(0, val_lo);
    cpu.write_register(1, val_hi);
    mem.write_u32(0x1000, encode_msr(3, 0, 2, 1, 0, 0)); // MSR APIAKeyLo_EL1, X0
    mem.write_u32(0x1004, encode_msr(3, 0, 2, 1, 1, 1)); // MSR APIAKeyHi_EL1, X1
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    cpu.step(mem);

    const auto& kr = cpu.get_pac_keyring();
    assert(kr.apia.lo == val_lo);
    assert(kr.apia.hi == val_hi);
    (void)kr;

    // Read back via MRS into X2 and X3
    mem.write_u32(0x1008, encode_mrs(3, 0, 2, 1, 0, 2)); // MRS X2, APIAKeyLo_EL1
    mem.write_u32(0x100C, encode_mrs(3, 0, 2, 1, 1, 3)); // MRS X3, APIAKeyHi_EL1
    cpu.set_program_counter(0x1008);
    cpu.step(mem);
    cpu.step(mem);

    assert(cpu.read_register(2) == val_lo);
    assert(cpu.read_register(3) == val_hi);

    // 2. Test APDAKey
    const std::uint64_t da_lo = 0x1111222233334444ULL;
    const std::uint64_t da_hi = 0x5555666677778888ULL;
    cpu.write_register(4, da_lo);
    cpu.write_register(5, da_hi);
    mem.write_u32(0x1010, encode_msr(3, 0, 2, 2, 0, 4)); // MSR APDAKeyLo_EL1, X4
    mem.write_u32(0x1014, encode_msr(3, 0, 2, 2, 1, 5)); // MSR APDAKeyHi_EL1, X5
    cpu.set_program_counter(0x1010);
    cpu.step(mem);
    cpu.step(mem);

    assert(cpu.get_pac_keyring().apda.lo == da_lo);
    assert(cpu.get_pac_keyring().apda.hi == da_hi);

    // Read back via MRS into X6 and X7
    mem.write_u32(0x1018, encode_mrs(3, 0, 2, 2, 0, 6)); // MRS X6, APDAKeyLo_EL1
    mem.write_u32(0x101C, encode_mrs(3, 0, 2, 2, 1, 7)); // MRS X7, APDAKeyHi_EL1
    cpu.set_program_counter(0x1018);
    cpu.step(mem);
    cpu.step(mem);

    assert(cpu.read_register(6) == da_lo);
    assert(cpu.read_register(7) == da_hi);

    std::cout << "[PASS] test_pac_key_msr_mrs_roundtrip" << std::endl;
}

void test_pac_el0_privilege_trap() {
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_current_el(0); // Run at EL0
    Memory mem(0x10000);
    const auto init_apia_lo = cpu.get_pac_keyring().apia.lo;

    cpu.write_register(0, 0x12345678ULL);
    mem.write_u32(0x1000, encode_msr(3, 0, 2, 1, 0, 0)); // MSR APIAKeyLo_EL1, X0
    cpu.set_program_counter(0x1000);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());
    assert(cpu.get_pac_keyring().apia.lo == init_apia_lo); // Key unchanged
    assert(cpu.get_pac_keyring().apia.lo != 0x12345678ULL);

    // Attempt MRS at EL0
    cpu.set_current_el(0);
    mem.write_u32(0x1004, encode_mrs(3, 0, 2, 1, 0, 1)); // MRS X1, APIAKeyLo_EL1
    cpu.set_program_counter(0x1004);
    cpu.step(mem);

    assert(cpu.exception_taken_in_step());

    std::cout << "[PASS] test_pac_el0_privilege_trap" << std::endl;
}

void test_pac_keyring_direct_access() {
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    Memory mem(0x10000);

    PacKeyRing kr{};
    kr.apga.lo = 0xCAFEBABE12345678ULL;
    kr.apga.hi = 0xDEADBEEF87654321ULL;
    cpu.set_pac_keyring(kr);

    assert(cpu.get_pac_keyring().apga.lo == 0xCAFEBABE12345678ULL);
    assert(cpu.get_pac_keyring().apga.hi == 0xDEADBEEF87654321ULL);

    // Verify MRS APGAKeyLo_EL1 and APGAKeyHi_EL1 retrieve the direct keyring values
    mem.write_u32(0x1000, encode_mrs(3, 0, 2, 3, 0, 10)); // MRS X10, APGAKeyLo_EL1
    mem.write_u32(0x1004, encode_mrs(3, 0, 2, 3, 1, 11)); // MRS X11, APGAKeyHi_EL1
    cpu.set_program_counter(0x1000);
    cpu.step(mem);
    cpu.step(mem);

    assert(cpu.read_register(10) == 0xCAFEBABE12345678ULL);
    assert(cpu.read_register(11) == 0xDEADBEEF87654321ULL);

    std::cout << "[PASS] test_pac_keyring_direct_access" << std::endl;
}

int main() {
    std::cout << "--- Stage 199.9: PAC Key & Register Architecture Tests ---" << std::endl;
    test_pac_sysreg_descriptors();
    test_pac_privilege_checking();
    test_pac_key_msr_mrs_roundtrip();
    test_pac_el0_privilege_trap();
    test_pac_keyring_direct_access();
    std::cout << "All Stage 199.9 tests passed successfully!" << std::endl;
    return 0;
}
