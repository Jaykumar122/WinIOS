// ============================================================================
// Stage 193.6: System Register Access and Privilege Enforcement Tests
// ============================================================================

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

#include "cpu/cpu.h"
#include "cpu/sysreg.h"
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
using namespace winios::sysreg;

static bool test_sysreg_descriptors_metadata() {
    struct ExpectedMeta {
        SysRegId id;
        std::string_view name;
        unsigned min_el;
        bool readable;
        bool writable;
    };

    const ExpectedMeta expected[] = {
        { SysRegId::CurrentEL, "CurrentEL", 1, true,  false },
        { SysRegId::VBAR_EL1,  "VBAR_EL1",  1, true,  true  },
        { SysRegId::ESR_EL1,   "ESR_EL1",   1, true,  true  },
        { SysRegId::ELR_EL1,   "ELR_EL1",   1, true,  true  },
        { SysRegId::SPSR_EL1,  "SPSR_EL1",  1, true,  true  },
        { SysRegId::FAR_EL1,   "FAR_EL1",   1, true,  true  },
        { SysRegId::SCTLR_EL1, "SCTLR_EL1", 1, true,  true  },
        { SysRegId::TTBR0_EL1, "TTBR0_EL1", 1, true,  true  },
        { SysRegId::TTBR1_EL1, "TTBR1_EL1", 1, true,  true  },
        { SysRegId::TCR_EL1,   "TCR_EL1",   1, true,  true  },
        { SysRegId::MAIR_EL1,  "MAIR_EL1",  1, true,  true  },
        { SysRegId::TPIDR_EL0, "TPIDR_EL0", 0, true,  true  },
        { SysRegId::TPIDR_EL1, "TPIDR_EL1", 1, true,  true  },
        { SysRegId::SP_EL0,    "SP_EL0",    1, true,  true  },
        { SysRegId::SPSel,     "SPSel",     1, true,  true  },
    };

    for (const auto& exp : expected) {
        const auto* desc_by_id = find_descriptor_by_id(exp.id);
        CHECK(desc_by_id != nullptr, "Descriptor by ID must exist");
        CHECK(desc_by_id->name == exp.name, "Descriptor name mismatch");
        CHECK(desc_by_id->min_el == exp.min_el, "min_el mismatch");
        CHECK(desc_by_id->is_readable == exp.readable, "readable mismatch");
        CHECK(desc_by_id->is_writable == exp.writable, "writable mismatch");

        const auto* desc_by_name = find_descriptor_by_name(exp.name);
        CHECK(desc_by_name == desc_by_id, "find_descriptor_by_name must match find_descriptor_by_id");
    }

    std::cout << "[PASS] test_sysreg_descriptors_metadata\n";
    return true;
}

static bool test_el1_guest_read_write_access() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);

    const std::uint64_t code_addr = 0x1000ULL;

    struct RegRwTest {
        std::string_view name;
        std::uint32_t msr_insn;
        std::uint32_t mrs_insn;
        std::uint64_t write_val;
        std::uint64_t expected_val;
    };

    const RegRwTest tests[] = {
        { "VBAR_EL1",  0xD518C000u, 0xD538C001u, 0xFFFF000012345000ULL, 0xFFFF000012345000ULL },
        { "ESR_EL1",   0xD5185200u, 0xD5385201u, 0x92000000ULL,          0x92000000ULL },
        { "ELR_EL1",   0xD5184020u, 0xD5384021u, 0x0000000000401000ULL,  0x0000000000401000ULL },
        { "SPSR_EL1",  0xD5184000u, 0xD5384001u, 0x60000000ULL,          0x60000000ULL },
        { "FAR_EL1",   0xD5186000u, 0xD5386001u, 0x00007FFFF0002000ULL,  0x00007FFFF0002000ULL },
        { "SCTLR_EL1", 0xD5181000u, 0xD5381001u, 0x0000000000050838ULL,  0x0000000000050838ULL },
        { "TTBR0_EL1", 0xD5182000u, 0xD5382001u, 0x0000000180004000ULL,  0x0000000180004000ULL },
        { "TTBR1_EL1", 0xD5182020u, 0xD5382021u, 0x0000000180008000ULL,  0x0000000180008000ULL },
        { "TCR_EL1",   0xD5182040u, 0xD5382041u, 0x00000012B5193519ULL,  0x00000012B5193519ULL },
        { "MAIR_EL1",  0xD518A200u, 0xD538A201u, 0x00000000004404FFULL,  0x00000000004404FFULL },
        { "TPIDR_EL0", 0xD51BD040u, 0xD53BD041u, 0x00007FFFF1234000ULL,  0x00007FFFF1234000ULL },
        { "TPIDR_EL1", 0xD518D080u, 0xD538D081u, 0xFFFF800012345678ULL,  0xFFFF800012345678ULL },
        { "SP_EL0",    0xD5184100u, 0xD5384101u, 0x00007FFFFFFFE000ULL,  0x00007FFFFFFFE000ULL },
    };

    for (const auto& t : tests) {
        mem.write_u32(static_cast<std::size_t>(code_addr), t.msr_insn);
        mem.write_u32(static_cast<std::size_t>(code_addr + 4), t.mrs_insn);

        cpu.write_register(0, t.write_val);
        cpu.write_register(1, 0);
        cpu.set_program_counter(code_addr);

        cpu.step(mem); // MSR
        cpu.step(mem); // MRS

        CHECK(cpu.read_register(1) == t.expected_val,
              ("EL1 read-write check failed for " + std::string(t.name)).c_str());
    }

    // CurrentEL is read-only at EL1: MRS X1, CurrentEL (0xD5384241u)
    mem.write_u32(static_cast<std::size_t>(code_addr), 0xD5384241u);
    cpu.write_register(1, 0);
    cpu.set_program_counter(code_addr);
    cpu.step(mem);
    CHECK(cpu.read_register(1) == 0x4ULL, "CurrentEL read at EL1 must return 0x4 (EL1)");

    std::cout << "[PASS] test_el1_guest_read_write_access\n";
    return true;
}

static bool test_el0_guest_read_write_privilege_rejection() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    const std::uint64_t user_code = 0x4000ULL;

    // EL1 registers that must NOT be accessible at EL0:
    const std::uint32_t unauthorized_insns[] = {
        0xD5384240u, // MRS X0, CurrentEL
        0xD538C000u, // MRS X0, VBAR_EL1
        0xD518C000u, // MSR VBAR_EL1, X0
        0xD5385200u, // MRS X0, ESR_EL1
        0xD5185200u, // MSR ESR_EL1, X0
        0xD5384020u, // MRS X0, ELR_EL1
        0xD5184020u, // MSR ELR_EL1, X0
        0xD5384000u, // MRS X0, SPSR_EL1
        0xD5184000u, // MSR SPSR_EL1, X0
        0xD5386000u, // MRS X0, FAR_EL1
        0xD5186000u, // MSR FAR_EL1, X0
        0xD5381000u, // MRS X0, SCTLR_EL1
        0xD5181000u, // MSR SCTLR_EL1, X0
        0xD5382000u, // MRS X0, TTBR0_EL1
        0xD5182000u, // MSR TTBR0_EL1, X0
        0xD5382020u, // MRS X0, TTBR1_EL1
        0xD5182020u, // MSR TTBR1_EL1, X0
        0xD5382040u, // MRS X0, TCR_EL1
        0xD5182040u, // MSR TCR_EL1, X0
        0xD538A200u, // MRS X0, MAIR_EL1
        0xD518A200u, // MSR MAIR_EL1, X0
        0xD538D080u, // MRS X0, TPIDR_EL1
        0xD518D080u, // MSR TPIDR_EL1, X0
        0xD5384100u, // MRS X0, SP_EL0
        0xD5184100u, // MSR SP_EL0, X0
        0xD5384200u, // MRS X0, SPSel
        0xD5184200u, // MSR SPSel, X0
    };

    for (std::uint32_t insn : unauthorized_insns) {
        mem.write_u32(static_cast<std::size_t>(user_code), insn);

        cpu.set_current_el(0);
        cpu.set_sp_sel(false);
        cpu.set_program_counter(user_code);

        cpu.step(mem);

        CHECK(cpu.get_current_el() == 1, "Must trap to EL1 on unauthorized sysreg access");
        CHECK(cpu.get_program_counter() == vbar + 0x400ULL, "PC must jump to Lower EL Sync vector");
        CHECK(cpu.get_elr_el1() == user_code, "ELR_EL1 must point to faulting instruction");
        const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
        CHECK(ec == 0x00u, "ESR_EL1 EC must be UndefinedInstruction (0x00)");
    }

    std::cout << "[PASS] test_el0_guest_read_write_privilege_rejection\n";
    return true;
}

static bool test_el0_permitted_access_tpidr_el0() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t user_code = 0x4000ULL;
    // 0x4000: MSR TPIDR_EL0, X0 (0xD51BD040u)
    // 0x4004: MRS X1, TPIDR_EL0 (0xD53BD041u)
    mem.write_u32(static_cast<std::size_t>(user_code + 0), 0xD51BD040u);
    mem.write_u32(static_cast<std::size_t>(user_code + 4), 0xD53BD041u);

    cpu.set_current_el(0);
    cpu.set_sp_sel(false);
    cpu.set_program_counter(user_code);
    cpu.write_register(0, 0x1234567890ABCDEFULL);
    cpu.write_register(1, 0);

    cpu.step(mem); // MSR
    CHECK(cpu.get_current_el() == 0, "Must remain at EL0 on permitted TPIDR_EL0 write");
    CHECK(cpu.get_program_counter() == user_code + 4, "PC must advance");

    cpu.step(mem); // MRS
    CHECK(cpu.get_current_el() == 0, "Must remain at EL0 on permitted TPIDR_EL0 read");
    CHECK(cpu.read_register(1) == 0x1234567890ABCDEFULL, "X1 must read back written TPIDR_EL0 value");

    std::cout << "[PASS] test_el0_permitted_access_tpidr_el0\n";
    return true;
}

static bool test_write_rejection_on_readonly_current_el() {
    CPU cpu;
    cpu.set_architectural_exceptions(true);
    Memory mem(0x200000ULL);

    const std::uint64_t vbar = 0x80000ULL;
    cpu.set_vbar_el1(vbar);

    const std::uint64_t code_addr = 0x10000ULL;
    // MSR CurrentEL, X0 (0xD5184240u)
    mem.write_u32(static_cast<std::size_t>(code_addr), 0xD5184240u);

    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_program_counter(code_addr);

    cpu.step(mem);

    // MSR CurrentEL is architecturally illegal: traps to Current EL SPx Sync vector
    CHECK(cpu.get_current_el() == 1, "Must remain at EL1");
    CHECK(cpu.get_program_counter() == vbar + 0x200ULL, "PC must jump to Current EL SPx Sync vector");
    CHECK(cpu.get_elr_el1() == code_addr, "ELR_EL1 must point to faulting MSR CurrentEL instruction");
    const auto ec = static_cast<std::uint32_t>((cpu.get_esr_el1() >> 26u) & 0x3Fu);
    CHECK(ec == 0x00u, "ESR_EL1 EC must be UndefinedInstruction (0x00)");

    std::cout << "[PASS] test_write_rejection_on_readonly_current_el\n";
    return true;
}

int main() {
    try {
        std::cout << "=== Running Stage 193.6: System Register Access and Privilege Enforcement Tests ===\n";
        if (!test_sysreg_descriptors_metadata()) return 1;
        if (!test_el1_guest_read_write_access()) return 1;
        if (!test_el0_guest_read_write_privilege_rejection()) return 1;
        if (!test_el0_permitted_access_tpidr_el0()) return 1;
        if (!test_write_rejection_on_readonly_current_el()) return 1;
        std::cout << "=== All Stage 193.6 Tests Passed (5/5) ===\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "EXCEPTION CAUGHT: " << ex.what() << "\n";
        return 1;
    }
}
