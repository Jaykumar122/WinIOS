// ============================================================================
// WinIOS Campaign 281: Master Engineering Execution
// Stages 281.15 - 281.19: Bounded Linked Execution of Genuine Upstream XNU
// cswitch.o Past MSR SSBS (0x07C) -> RETAA (0x0B4), _Call_continuation,
// _Switch_context, _Idle_load_context & Provenance Boundary Accounting
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "loader/macho_object_integrator.h"
#include "memory/memory.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios::cpu;
using namespace winios::loader;

fs::path locate_cswitch_object() {
    for (const fs::path& cand : {
             fs::path("D:/WinISO26/xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"),
             fs::path("../../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"),
             fs::path("../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o")}) {
        if (fs::exists(cand) && fs::file_size(cand) == 3336u) {
            return fs::weakly_canonical(cand);
        }
    }
    return {};
}

PacKeyRing default_test_keys() {
    PacKeyRing k{};
    k.apia = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    k.apib = {0x1111222233334444ULL, 0x5555666677778888ULL};
    k.apda = {0xAAAABBBBCCCCDDDDULL, 0x13579BDF02468ACEULL};
    k.apdb = {0x9999888877776666ULL, 0x5555444433332222ULL};
    k.apga = {0x4242424242424242ULL, 0x2424242424242424ULL};
    return k;
}

void setup_cpu(CPU& cpu, const PacKeyRing& keys) {
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_cpacr_el1(3ULL << 20u); // CPACR_EL1.FPEN = 0b11 (required by genuine cswitch.o FPCR & d8..d15)
    cpu.set_pac_keyring(keys);
    cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
}

struct ThreadMemoryLayout {
    std::uint64_t thread_va{0};
    std::uint64_t cpu_data_va{0};
    std::uint64_t kstack_va{0};
    std::uint64_t user_tp{0};
    std::uint64_t cthread_self{0};
    std::uint64_t saved_sp{0};
    std::uint64_t return_va{0};
    bool          saved_ssbs{false};
    std::uint32_t saved_fpcr{0};
};

void write_thread_context(Memory& mem, const CPU& cpu, const ThreadMemoryLayout& t) {
    mem.write_u64(t.thread_va + 0x1B8u, t.cpu_data_va);
    mem.write_u64(t.thread_va + 0x148u, t.cthread_self);
    mem.write_u64(t.cpu_data_va + 0x398u, t.user_tp);

    const std::uint64_t slot_va = t.thread_va + 0x158u;
    const std::uint64_t slot_mod = slot_va | (0x6D42ULL << 48u);
    mem.write_u64(slot_va, cpu.get_pac().sign_pointer(t.kstack_va, slot_mod, PacKeyType::DataA));

    // Callee-saved x19..x29
    for (std::uint64_t r = 19; r <= 29; ++r) {
        mem.write_u64(t.kstack_va + (r - 19u) * 8u, 0x1900000000ULL + r);
    }
    // x30 signed for RETAA (APIAKey, modifier = saved_sp)
    const std::uint64_t signed_lr = cpu.get_pac().sign_pointer(t.return_va, t.saved_sp, PacKeyType::InstructionA);
    mem.write_u64(t.kstack_va + 0x58u, signed_lr);
    mem.write_u64(t.kstack_va + 0x60u, t.saved_sp);
    mem.write_u8(t.kstack_va + 0x69u, t.saved_ssbs ? 1u : 0u);
    // d8..d15 at +0x70..+0xa8
    for (std::uint64_t d = 0; d < 8u; ++d) {
        mem.write_u64(t.kstack_va + 0x70u + d * 8u, 0xD800000000000000ULL + (d + 8u));
    }
    mem.write_u32(t.kstack_va + 0xB0u, t.saved_fpcr);
}

int test_genuine_machine_load_context_ssbs_and_retaa(const MachOObjectParseResult& parsed) {
    // Pure genuine __TEXT,__text without stubs touched anywhere in _machine_load_context.
    MachOIntegrationOptions opts;
    opts.text_base_va      = 0x00004000ULL;
    opts.stub_base_va      = 0x00008000ULL;
    opts.virtual_base_bias = 0x0ULL;
    opts.require_all_relocations_resolved = false; // no externs reached in _machine_load_context

    Memory mem(0x100000ULL);
    const auto staged = MachOObjectIntegrator::stage_in_memory(parsed, opts, mem);
    TEST_ASSERT(staged.is_success(), "Staging cswitch.o for _machine_load_context failed");
    std::uint64_t load_ctx_va = 0;
    TEST_ASSERT(staged.find_symbol_va("_machine_load_context", load_ctx_va),
                "Missing _machine_load_context symbol");

    const PacKeyRing keys = default_test_keys();

    // Case A: Saved SSBS=1 (previously blocked at 0x07C in C280) -> runs all the way to RETAA!
    {
        CPU cpu;
        setup_cpu(cpu, keys);
        cpu.set_pstate_ssbs(false);

        ThreadMemoryLayout layout;
        layout.thread_va    = 0x40000ULL;
        layout.cpu_data_va  = 0x50000ULL;
        layout.kstack_va    = 0x60000ULL;
        layout.saved_sp     = 0x70000ULL;
        layout.user_tp      = 0x0000000100012340ULL;
        layout.cthread_self = 0x0000000100056780ULL;
        layout.return_va    = 0x80000ULL;
        layout.saved_ssbs   = true;
        layout.saved_fpcr   = 0x01000000u; // DN bit set -> differs from reset fpcr=0 -> exercises msr fpcr, x2
        write_thread_context(mem, cpu, layout);
        mem.write_u32(layout.return_va, 0xD503201Fu);

        cpu.write_register(0, layout.thread_va);
        cpu.write_register(1, layout.kstack_va);
        cpu.set_program_counter(load_ctx_va);

        bool crossed_ssbs = false;
        bool returned_via_retaa = false;
        for (int s = 0; s < 80; ++s) {
            const std::uint64_t pc = cpu.get_program_counter();
            if (pc == load_ctx_va + 0x07Cu) {
                crossed_ssbs = true;
            }
            cpu.step(mem);
            if (cpu.get_program_counter() == layout.return_va) {
                returned_via_retaa = true;
                break;
            }
        }
        TEST_ASSERT(crossed_ssbs, "_machine_load_context must cross 0x07C MSR SSBS");
        TEST_ASSERT(returned_via_retaa, "_machine_load_context must return cleanly via 0x0B4 RETAA");
        TEST_ASSERT(cpu.get_pstate_ssbs(), "PSTATE.SSBS must be restored to 1");
        TEST_ASSERT(cpu.get_stack_pointer() == layout.saved_sp, "SP must be restored");
        TEST_ASSERT(cpu.get_fpcr() == layout.saved_fpcr, "FPCR must be restored");
        TEST_ASSERT(cpu.get_tpidr_el1() == layout.thread_va, "TPIDR_EL1 must equal thread_va");
        TEST_ASSERT(mem.read_u64(layout.cpu_data_va + 0x28u) == layout.thread_va,
                    "cpu_data->cpu_active_thread (+0x28) must equal thread_va");
        TEST_ASSERT(cpu.get_tpidr_el0() == layout.user_tp, "TPIDR_EL0 must match");
        TEST_ASSERT(cpu.get_tpidrro_el0() == layout.cthread_self, "TPIDRRO_EL0 must match");
        TEST_ASSERT(cpu.read_register(0) == 0ULL, "_machine_load_context sets x0 = 0 on return");
        for (std::size_t r = 19; r <= 29; ++r) {
            TEST_ASSERT(cpu.read_register(r) == 0x1900000000ULL + r, "Callee-saved GPR mismatch");
        }
        for (std::size_t d = 0; d < 8; ++d) {
            TEST_ASSERT(cpu.read_d_register(d + 8) == 0xD800000000000000ULL + (d + 8u),
                        "Callee-saved d8..d15 mismatch");
        }
    }

    // Case B: PSTATE.SSBS transition true -> false when saved_ssbs == false
    {
        CPU cpu;
        setup_cpu(cpu, keys);
        cpu.set_pstate_ssbs(true);

        ThreadMemoryLayout layout;
        layout.thread_va    = 0x42000ULL;
        layout.cpu_data_va  = 0x52000ULL;
        layout.kstack_va    = 0x62000ULL;
        layout.saved_sp     = 0x72000ULL;
        layout.return_va    = 0x82000ULL;
        layout.saved_ssbs   = false;
        layout.saved_fpcr   = 0u;
        write_thread_context(mem, cpu, layout);

        cpu.write_register(0, layout.thread_va);
        cpu.write_register(1, layout.kstack_va);
        cpu.set_program_counter(load_ctx_va);
        bool done = false;
        for (int s = 0; s < 80; ++s) {
            cpu.step(mem);
            if (cpu.get_program_counter() == layout.return_va) {
                done = true;
                break;
            }
        }
        TEST_ASSERT(done && !cpu.get_pstate_ssbs(), "_machine_load_context must clear SSBS when saved byte is 0");
    }

    return 0;
}

int test_idle_load_and_switch_context(const MachOObjectParseResult& parsed) {
    MachOIntegrationOptions opts;
    opts.text_base_va      = 0x00004000ULL;
    opts.stub_base_va      = 0x00008000ULL;
    opts.virtual_base_bias = 0x0ULL;
    opts.require_all_relocations_resolved = false;

    Memory mem(0x100000ULL);
    const auto staged = MachOObjectIntegrator::stage_in_memory(parsed, opts, mem);
    TEST_ASSERT(staged.is_success(), "Staging failed");

    std::uint64_t idle_load_va = 0, switch_va = 0, set_cur_va = 0;
    TEST_ASSERT(staged.find_symbol_va("_Idle_load_context", idle_load_va) &&
                staged.find_symbol_va("_Switch_context", switch_va) &&
                staged.find_symbol_va("_machine_set_current_thread", set_cur_va),
                "Expected cswitch symbols must resolve");

    const PacKeyRing keys = default_test_keys();

    // 1. _Idle_load_context crosses 0x464 MSR SSBS -> 0x498 RETAA
    {
        CPU cpu;
        setup_cpu(cpu, keys);
        ThreadMemoryLayout layout;
        layout.thread_va   = 0x40000ULL;
        layout.cpu_data_va = 0x50000ULL;
        layout.kstack_va   = 0x60000ULL;
        layout.saved_sp    = 0x70000ULL;
        layout.return_va   = 0x84000ULL;
        layout.saved_ssbs  = true;
        write_thread_context(mem, cpu, layout);

        cpu.set_tpidr_el1(layout.thread_va);
        cpu.set_program_counter(idle_load_va);
        bool ok = false;
        for (int s = 0; s < 60; ++s) {
            cpu.step(mem);
            if (cpu.get_program_counter() == layout.return_va) {
                ok = true;
                break;
            }
        }
        TEST_ASSERT(ok && cpu.get_pstate_ssbs(), "_Idle_load_context must restore SSBS and return via RETAA");
    }

    // 2. _Switch_context save + restore (x1 == 0): verifies MRS X4, SSBS captures PSTATE.SSBS into old_thread[+0x69],
    // switches to new_thread, and restores SSBS + RETAA cleanly. Note: PACIASP at 0x168 is architectural HINT NOP in WinIOS.
    {
        CPU cpu;
        setup_cpu(cpu, keys);
        cpu.set_pstate_ssbs(true);
        cpu.set_fpcr(0x02000000u);
        cpu.write_register(19, 0x1111222233334444ULL);
        cpu.write_register(30, 0x88888ULL);
        cpu.set_stack_pointer(0x78000ULL);

        ThreadMemoryLayout old_t;
        old_t.thread_va   = 0x40000ULL;
        old_t.cpu_data_va = 0x50000ULL;
        old_t.kstack_va   = 0x60000ULL;
        old_t.saved_sp    = 0x70000ULL;
        old_t.return_va   = 0x80000ULL;
        write_thread_context(mem, cpu, old_t);

        ThreadMemoryLayout new_t;
        new_t.thread_va    = 0x44000ULL;
        new_t.cpu_data_va  = 0x50000ULL;
        new_t.kstack_va    = 0x64000ULL;
        new_t.saved_sp     = 0x74000ULL;
        new_t.return_va    = 0x8C000ULL;
        new_t.saved_ssbs   = false;
        new_t.user_tp      = 0x0000000200001000ULL;
        new_t.cthread_self = 0x0000000200002000ULL;
        write_thread_context(mem, cpu, new_t);

        cpu.write_register(0, old_t.thread_va);
        cpu.write_register(1, 0ULL); // save old context
        cpu.write_register(2, new_t.thread_va);
        cpu.set_program_counter(switch_va);

        bool done = false;
        for (int s = 0; s < 120; ++s) {
            cpu.step(mem);
            if (cpu.get_program_counter() == new_t.return_va) {
                done = true;
                break;
            }
        }
        TEST_ASSERT(done, "_Switch_context must reach new_thread return via RETAA");
        TEST_ASSERT(mem.read_u8(old_t.kstack_va + 0x69u) == 1u,
                    "_Switch_context MRS X4, SSBS must save old SSBS=1");
        TEST_ASSERT(mem.read_u64(old_t.kstack_va + 0x00u) == 0x1111222233334444ULL,
                    "_Switch_context must save old x19");
        TEST_ASSERT(mem.read_u32(old_t.kstack_va + 0xB0u) == 0x02000000u,
                    "_Switch_context must save old FPCR");
        TEST_ASSERT(!cpu.get_pstate_ssbs(), "_Switch_context must install new_thread SSBS=0");
        TEST_ASSERT(cpu.get_tpidr_el1() == new_t.thread_va, "TPIDR_EL1 updated to new_thread");
    }

    // 3. _machine_set_current_thread returns cleanly via RET
    {
        CPU cpu;
        setup_cpu(cpu, keys);
        cpu.write_register(0, 0x44000ULL);
        cpu.write_register(30, 0x90000ULL);
        cpu.set_program_counter(set_cur_va);
        for (int s = 0; s < 8; ++s) {
            cpu.step(mem);
        }
        TEST_ASSERT(cpu.get_program_counter() == 0x90000ULL && cpu.get_tpidr_el1() == 0x44000ULL,
                    "_machine_set_current_thread must set TPIDR_EL1 and return");
    }

    return 0;
}

int test_call_continuation_unlinked_and_stub_bounded(const MachOObjectParseResult& parsed) {
    const PacKeyRing keys = default_test_keys();

    // Case 1: x3 != 0 with UNLINKED external relocations -> must honestly stop/loop on unpatched bl #0 at +0x114
    {
        MachOIntegrationOptions unlinked_opts;
        unlinked_opts.text_base_va      = 0x00004000ULL;
        unlinked_opts.stub_base_va      = 0x00008000ULL;
        unlinked_opts.virtual_base_bias = 0x0ULL;
        unlinked_opts.require_all_relocations_resolved = false;

        Memory mem(0x100000ULL);
        const auto staged = MachOObjectIntegrator::stage_in_memory(parsed, unlinked_opts, mem);
        TEST_ASSERT(staged.is_success() && !staged.unresolved_symbols.empty(),
                    "Unlinked stage must track unresolved symbols");
        std::uint64_t cont_entry = 0;
        TEST_ASSERT(staged.find_symbol_va("_Call_continuation", cont_entry), "Find _Call_continuation");

        CPU cpu;
        setup_cpu(cpu, keys);
        ThreadMemoryLayout layout;
        layout.thread_va  = 0x40000ULL;
        layout.kstack_va  = 0x60000ULL;
        layout.cpu_data_va = 0x50000ULL;
        write_thread_context(mem, cpu, layout);

        const std::uint64_t signed_cont = cpu.get_pac().sign_pointer(0x90000ULL, 0xD507ULL, PacKeyType::InstructionA);
        cpu.set_tpidr_el1(layout.thread_va);
        cpu.write_register(0, signed_cont);
        cpu.write_register(1, 0x1234ULL);
        cpu.write_register(2, 1ULL);
        cpu.write_register(3, 1ULL); // enable_interrupts = 1 -> hits unpatched bl at +0x114
        cpu.set_program_counter(cont_entry);

        const std::uint64_t reloc_bl_va = unlinked_opts.text_base_va + 0x114u;
        bool blocked_at_unpatched_reloc = false;
        for (int s = 0; s < 40; ++s) {
            const std::uint64_t before = cpu.get_program_counter();
            cpu.step(mem);
            if (before == reloc_bl_va && cpu.get_program_counter() == reloc_bl_va) {
                blocked_at_unpatched_reloc = true;
                break;
            }
        }
        TEST_ASSERT(blocked_at_unpatched_reloc,
                    "Unlinked _Call_continuation (x3=1) must halt honestly at unpatched bl (+0x114)");
    }

    // Case 2: x3 != 0 explicitly linked to TestHarnessStubs via ARM64_RELOC_BRANCH26 ->
    // _ml_set_interrupts_enabled(1) stub -> blraa cont -> cont returns -> _thread_terminate(thread) stub
    {
        MachOIntegrationOptions bound_opts;
        bound_opts.text_base_va      = 0x00004000ULL;
        bound_opts.stub_base_va      = 0x00008000ULL;
        bound_opts.virtual_base_bias = 0x0ULL;
        bound_opts.require_all_relocations_resolved = true;
        // Stubs use caller-saved x15/x16 sentinel registers cleanly:
        // _ml_set_interrupts_enabled: ADD X15, X0, #0x400 ; RET (0x9110000F, 0xD65F03C0)
        // _thread_terminate: MOV X16, #0x7777 ; RET (0xD28EEEE0 | 16 = 0xD28EEEF0, 0xD65F03C0)
        bound_opts.external_bindings = {
            {"_ml_set_interrupts_enabled", SymbolProvenanceClassification::TestHarnessStub,
             {0x9110000Fu, 0xD65F03C0u}, 0, "marks x15 = enable + 0x400"},
            {"_thread_terminate", SymbolProvenanceClassification::TestHarnessStub,
             {0xD28EEEF0u, 0xD65F03C0u}, 0, "marks x16 = 0x7777"},
            {"_cpu_doshutdown", SymbolProvenanceClassification::TestHarnessStub,
             {0xD65F03C0u}, 0, "ret stub"},
            {"_cpu_idle", SymbolProvenanceClassification::TestHarnessStub,
             {0xD65F03C0u}, 0, "ret stub"}
        };

        Memory mem(0x100000ULL);
        const auto staged = MachOObjectIntegrator::stage_in_memory(parsed, bound_opts, mem);
        TEST_ASSERT(staged.is_success() && staged.has_test_harness_stubs,
                    "Bounded stub link must succeed and flag has_test_harness_stubs");

        std::uint64_t cont_entry = 0, ml_stub_va = 0, term_stub_va = 0;
        TEST_ASSERT(staged.find_symbol_va("_Call_continuation", cont_entry) &&
                    staged.find_symbol_va("_ml_set_interrupts_enabled", ml_stub_va) &&
                    staged.find_symbol_va("_thread_terminate", term_stub_va),
                    "All symbols must resolve");

        CPU cpu;
        setup_cpu(cpu, keys);
        ThreadMemoryLayout layout;
        layout.thread_va   = 0x40000ULL;
        layout.cpu_data_va = 0x50000ULL;
        layout.kstack_va   = 0x60000ULL;
        write_thread_context(mem, cpu, layout);

        const std::uint64_t cont_target = 0x94000ULL;
        // Guest continuation body: ADD X14, X0, X1 ; RET (0x8B01000E, 0xD65F03C0)
        mem.write_u32(cont_target + 0u, 0x8B01000Eu);
        mem.write_u32(cont_target + 4u, 0xD65F03C0u);
        const std::uint64_t signed_cont = cpu.get_pac().sign_pointer(cont_target, 0xD507ULL, PacKeyType::InstructionA);

        const std::uint64_t final_halt = 0x98000ULL;
        mem.write_u32(final_halt, 0xD503201Fu);

        cpu.set_tpidr_el1(layout.thread_va);
        cpu.write_register(0, signed_cont);
        cpu.write_register(1, 0x1000ULL);
        cpu.write_register(2, 0x234ULL);
        cpu.write_register(3, 1ULL); // enable_interrupts = 1
        cpu.write_register(30, final_halt); // if _thread_terminate executes RET, returns here
        cpu.set_program_counter(cont_entry);

        std::size_t upstream_steps = 0;
        std::size_t stub_steps = 0;
        bool reached_term = false;
        for (int s = 0; s < 60; ++s) {
            const std::uint64_t pc = cpu.get_program_counter();
            const auto cls = staged.classify_va(pc);
            if (cls == SymbolProvenanceClassification::GenuineUpstream) {
                ++upstream_steps;
            } else if (cls == SymbolProvenanceClassification::TestHarnessStub) {
                ++stub_steps;
            }
            if (pc == term_stub_va) {
                reached_term = true;
                cpu.step(mem);
                cpu.step(mem);
                break;
            }
            cpu.step(mem);
        }
        TEST_ASSERT(reached_term, "_Call_continuation must execute stub -> cont -> _thread_terminate");
        TEST_ASSERT(upstream_steps > 20u && stub_steps >= 2u,
                    "Provenance accounting must track GenuineUpstream and TestHarnessStub steps separately");
        TEST_ASSERT(cpu.read_register(15) == 0x401ULL,
                    "_ml_set_interrupts_enabled TestHarnessStub must receive x0=1");
        TEST_ASSERT(cpu.read_register(14) == 0x1234ULL,
                    "Continuation must receive param=0x1000, wait_result=0x234");
        TEST_ASSERT(cpu.read_register(16) == 0x7777ULL && cpu.read_register(0) == layout.thread_va,
                    "_thread_terminate TestHarnessStub must receive thread_va in X0");
    }

    return 0;
}

} // namespace

int main() {
    try {
        const fs::path cswitch_path = locate_cswitch_object();
        TEST_ASSERT(!cswitch_path.empty(), "Genuine cswitch.o missing");
        const auto parsed = MachOObjectIntegrator::parse_object_file(cswitch_path.string());
        TEST_ASSERT(parsed.is_success(), "Failed to parse genuine cswitch.o");

        if (test_genuine_machine_load_context_ssbs_and_retaa(parsed) != 0) {
            return 1;
        }
        if (test_idle_load_and_switch_context(parsed) != 0) {
            return 1;
        }
        if (test_call_continuation_unlinked_and_stub_bounded(parsed) != 0) {
            return 1;
        }

        std::cout << "[PASS] stage281_xnu_cswitch_execution_tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled std::exception in stage281_xnu_cswitch_execution_tests: "
                  << e.what() << std::endl;
        return 1;
    }
}
