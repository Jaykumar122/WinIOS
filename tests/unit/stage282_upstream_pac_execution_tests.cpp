// ============================================================================
// WinIOS Campaign 282: Extended Genuine Upstream XNU Execution with Live
// ARM64e PAC (PACIASP/PACIBSP/AUTDA/RETAA/RETAB/BRAA) Across Linked Objects
// (cswitch.o + cpu_common.o + cpu.o + loose_ends.o + machine_routines_apple.o)
// Stages 282.15 - 282.21
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_core_state.h"
#include "loader/macho_object_integrator.h"
#include "memory/memory.h"
#include "replay/replay_state_hash.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " \
                  << __FILE__ << ":" << __LINE__ << std::endl; \
        return 1; \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::replay;
using namespace winios::snapshot;

fs::path find_existing(std::initializer_list<const char*> cands) {
    for (const char* p : cands) {
        if (fs::exists(p)) {
            return fs::weakly_canonical(p);
        }
    }
    return {};
}

PacKeyRing default_pac_keys() {
    PacKeyRing k{};
    k.apia = {0x1020304050607080ULL, 0x0919293949596979ULL};
    k.apib = {0xAABBCCDDEEFF0011ULL, 0x2233445566778899ULL};
    k.apda = {0x13579BDF02468ACEULL, 0xFDB97531ECA86420ULL};
    k.apdb = {0x9E3779B97F4A7C15ULL, 0xD1B54A32D192ED03ULL};
    k.apga = {0x4242424242424242ULL, 0x2424242424242424ULL};
    return k;
}

void init_pac_cpu(CPU& cpu, const PacKeyRing& keys) {
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x000B0000ULL);
    cpu.set_cpacr_el1(3ULL << 20u); // FPEN = 0b11
    // Enable BOTH IA (bit 31) and IB (bit 30) keys for live HINT PAC execution!
    cpu.set_sctlr_el1((1ULL << 31u) | (1ULL << 30u));
    cpu.set_pac_keyring(keys);
    cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
}

struct ThreadCtx {
    std::uint64_t thread_va{0};
    std::uint64_t cpu_data_va{0};
    std::uint64_t processor_va{0};
    std::uint64_t kstack_va{0};
    std::uint64_t int_stack_top{0};
    std::uint64_t saved_sp{0};
    std::uint64_t return_va{0};
    std::uint64_t user_tp{0};
    std::uint64_t cthread_self{0};
    bool          saved_ssbs{false};
    std::uint32_t saved_fpcr{0};
    std::uint64_t reg_seed{0};
};

void install_thread_ctx(Memory& mem, const CPU& cpu, const ThreadCtx& t) {
    mem.write_u64(t.thread_va + 0x1B8u, t.cpu_data_va);
    mem.write_u64(t.thread_va + 0x1B0u, t.processor_va);
    mem.write_u64(t.thread_va + 0x148u, t.cthread_self);
    mem.write_u64(t.cpu_data_va + 0x398u, t.user_tp);

    // _Shutdown_context and _Idle_context load and AUTDA [cpu_data + 0x10] (tag 0xE94D)
    const std::uint64_t istk_slot_va = t.cpu_data_va + 0x10u;
    const std::uint64_t istk_mod = istk_slot_va | (0xE94DULL << 48u);
    mem.write_u64(istk_slot_va,
                  cpu.get_pac().sign_pointer(t.int_stack_top, istk_mod, PacKeyType::DataA));

    // PAC-signed kstack pointer at [thread_va + 0x158] (tag 0x6D42)
    const std::uint64_t kstk_slot_va = t.thread_va + 0x158u;
    const std::uint64_t kstk_mod = kstk_slot_va | (0x6D42ULL << 48u);
    mem.write_u64(kstk_slot_va,
                  cpu.get_pac().sign_pointer(t.kstack_va, kstk_mod, PacKeyType::DataA));

    for (std::uint64_t r = 19; r <= 29; ++r) {
        mem.write_u64(t.kstack_va + (r - 19u) * 8u, t.reg_seed + r);
    }
    const std::uint64_t signed_lr =
        cpu.get_pac().sign_pointer(t.return_va, t.saved_sp, PacKeyType::InstructionA);
    mem.write_u64(t.kstack_va + 0x58u, signed_lr);
    mem.write_u64(t.kstack_va + 0x60u, t.saved_sp);
    mem.write_u8(t.kstack_va + 0x69u, t.saved_ssbs ? 1u : 0u);
    for (std::uint64_t d = 0; d < 8u; ++d) {
        mem.write_u64(t.kstack_va + 0x70u + d * 8u, t.reg_seed + 0xD00ULL + d);
    }
    mem.write_u32(t.kstack_va + 0xB0u, t.saved_fpcr);
}

int test_switch_context_live_paciasp_retaa_roundtrip(const MachOMultiObjectLinkResult& linked,
                                                     Memory& mem,
                                                     const PacKeyRing& keys) {
    std::uint64_t switch_va = 0;
    TEST_ASSERT(linked.find_symbol_va("_Switch_context", switch_va), "Find _Switch_context");

    CPU cpu;
    init_pac_cpu(cpu, keys);

    ThreadCtx a;
    a.thread_va    = 0x00060000ULL;
    a.cpu_data_va  = 0x00064000ULL;
    a.kstack_va    = 0x00068000ULL;
    a.saved_sp     = 0x00070000ULL;
    a.return_va    = 0x00080000ULL; // post-switch resume PC inside A
    a.user_tp      = 0x0000000100001110ULL;
    a.cthread_self = 0x0000000100002220ULL;
    a.saved_ssbs   = true;
    a.saved_fpcr   = 0x01000000u;
    a.reg_seed     = 0xAA00000000ULL;
    install_thread_ctx(mem, cpu, a);

    ThreadCtx b;
    b.thread_va    = 0x00061000ULL;
    b.cpu_data_va  = 0x00065000ULL;
    b.kstack_va    = 0x00069000ULL;
    b.saved_sp     = 0x00072000ULL;
    b.return_va    = 0x00082000ULL;
    b.user_tp      = 0x0000000100003330ULL;
    b.cthread_self = 0x0000000100004440ULL;
    b.saved_ssbs   = false;
    b.saved_fpcr   = 0x02000000u;
    b.reg_seed     = 0xBB00000000ULL;
    install_thread_ctx(mem, cpu, b);

    // At 0x82000 (Thread B entry): immediately switch back to Thread A via BL _Switch_context:
    // Thread B sets x0=b.thread_va, x1=0, x2=a.thread_va, x30=0x82020 (unused), calls _Switch_context
    mem.write_u32(0x00080000ULL, 0xD503201Fu); // NOP sentinel for Thread A resume
    mem.write_u32(0x00082000ULL, 0xD503201Fu); // B stops cleanly first so we inspect A->B save!

    // Thread A begins with caller-provided SP, unsigned LR=a.return_va, x19..x29 seeded, SSBS=1, FPCR=0x03000000
    cpu.set_tpidr_el1(a.thread_va);
    cpu.set_stack_pointer(a.saved_sp);
    cpu.set_pstate_ssbs(true);
    cpu.set_fpcr(0x03000000u);
    for (std::uint64_t r = 19; r <= 29; ++r) {
        cpu.write_register(r, 0xA1A2000000ULL + r);
    }
    cpu.write_register(30, a.return_va); // UNSIGNED raw LR! _Switch_context PACIASP will cryptographically sign it!
    cpu.write_register(0, a.thread_va);
    cpu.write_register(1, 0ULL);
    cpu.write_register(2, b.thread_va);
    cpu.set_program_counter(switch_va);

    bool hit_paciasp = false;
    bool landed_in_b = false;
    for (int s = 0; s < 100; ++s) {
        if (cpu.get_program_counter() == switch_va + 0x028u) { // 0x140 + 0x28 = 0x168 PACIASP
            hit_paciasp = true;
        }
        cpu.step(mem);
        TEST_ASSERT(!cpu.exception_taken_in_step(), "Unexpected trap during A->B switch");
        if (cpu.get_program_counter() == b.return_va) {
            landed_in_b = true;
            break;
        }
    }
    TEST_ASSERT(hit_paciasp && landed_in_b, "_Switch_context must execute PACIASP (0x168) and switch A -> B");

    const std::uint64_t saved_signed_a_lr = mem.read_u64(a.kstack_va + 0x58u);
    TEST_ASSERT(saved_signed_a_lr != a.return_va && (saved_signed_a_lr >> 48u) != 0u,
                "Live PACIASP with EnIA=1 must write PAC-signed A return address into kstack + 0x58");
    TEST_ASSERT(Arm64ePointerAuth::strip_pointer(saved_signed_a_lr) == a.return_va,
                "Stripped saved LR must match A's return VA");

    // Now switch B -> A! _Switch_context must authenticate saved_signed_a_lr via 0x274 RETAA!
    cpu.write_register(0, b.thread_va);
    cpu.write_register(1, 0ULL);
    cpu.write_register(2, a.thread_va);
    cpu.write_register(30, 0x00083000ULL);
    cpu.set_program_counter(switch_va);

    bool returned_to_a = false;
    for (int s = 0; s < 100; ++s) {
        cpu.step(mem);
        TEST_ASSERT(!cpu.exception_taken_in_step(), "B -> A RETAA authentication must succeed cleanly");
        if (cpu.get_program_counter() == a.return_va) {
            returned_to_a = true;
            break;
        }
    }
    TEST_ASSERT(returned_to_a, "B -> A must complete genuine PACIASP -> save -> RETAA round-trip");
    TEST_ASSERT(cpu.get_stack_pointer() == a.saved_sp && cpu.get_pstate_ssbs() &&
                cpu.get_fpcr() == 0x03000000u,
                "A's SP, SSBS, and live saved FPCR must be restored");
    for (std::uint64_t r = 19; r <= 29; ++r) {
        TEST_ASSERT(cpu.read_register(r) == 0xA1A2000000ULL + r,
                    "A's callee-saved GPRs from PACIASP save must survive full round-trip");
    }

    // Negative check: 1-bit tamper on saved_signed_a_lr must cause RETAA EC=0x1C trap!
    mem.write_u64(a.kstack_va + 0x58u, saved_signed_a_lr ^ (1ULL << 52u));
    cpu.set_tpidr_el1(b.thread_va);
    cpu.set_stack_pointer(b.saved_sp);
    cpu.write_register(0, b.thread_va);
    cpu.write_register(1, 0ULL);
    cpu.write_register(2, a.thread_va);
    cpu.set_program_counter(switch_va);
    bool trapped_pac = false;
    for (int s = 0; s < 100; ++s) {
        cpu.step(mem);
        if (cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu) {
            trapped_pac = true;
            break;
        }
    }
    TEST_ASSERT(trapped_pac, "Tampered kstack+0x58 PAC tag must trap at _Switch_context RETAA (EC=0x1C)");

    return 0;
}

int test_shutdown_and_idle_cross_object_chains(const MachOMultiObjectLinkResult& linked,
                                               Memory& mem,
                                               const PacKeyRing& keys) {
    std::uint64_t shutdown_va = 0, doshutdown_va = 0, idle_ctx_va = 0, cpu_idle_va = 0;
    std::uint64_t cur_proc_va = 0, idle_load_va = 0, idle_enable_va = 0;
    TEST_ASSERT(linked.find_symbol_va("_Shutdown_context", shutdown_va) &&
                linked.find_symbol_va("_cpu_doshutdown", doshutdown_va) &&
                linked.find_symbol_va("_Idle_context", idle_ctx_va) &&
                linked.find_symbol_va("_cpu_idle", cpu_idle_va) &&
                linked.find_symbol_va("_current_processor", cur_proc_va) &&
                linked.find_symbol_va("_Idle_load_context", idle_load_va) &&
                linked.find_symbol_va("_idle_enable", idle_enable_va),
                "All cross-object execution targets must resolve");

    // 1. _Shutdown_context (cswitch.o) -> _cpu_doshutdown (cpu_common.o) -> BRAA X2, X16 (disc 0x2ABE)
    {
        CPU cpu;
        init_pac_cpu(cpu, keys);

        ThreadCtx t;
        t.thread_va     = 0x00062000ULL;
        t.cpu_data_va   = 0x00066000ULL;
        t.kstack_va     = 0x0006A000ULL;
        t.int_stack_top = 0x00074000ULL;
        t.saved_sp      = 0x00071000ULL;
        t.return_va     = 0x00084000ULL;
        t.reg_seed      = 0xCC00000000ULL;
        install_thread_ctx(mem, cpu, t);

        const std::uint64_t doshutdown_cb = 0x00090000ULL;
        mem.write_u32(doshutdown_cb, 0xD503201Fu); // NOP landing sentinel
        // XNU arm64e ptrauth discriminator for void (*)(processor_t): #0x2ABE
        const std::uint64_t signed_cb =
            cpu.get_pac().sign_pointer(doshutdown_cb, 0x2ABEULL, PacKeyType::InstructionA);
        const std::uint64_t processor_arg = 0x0006C000ULL;

        cpu.set_tpidr_el1(t.thread_va);
        cpu.set_stack_pointer(t.saved_sp);
        cpu.write_register(0, signed_cb);
        cpu.write_register(1, processor_arg);
        cpu.write_register(30, t.return_va);
        cpu.set_program_counter(shutdown_va);

        bool entered_cswitch = false;
        bool entered_cpu_common = false;
        bool reached_cb = false;
        for (int s = 0; s < 100; ++s) {
            const std::uint64_t pc = cpu.get_program_counter();
            if (pc == shutdown_va) entered_cswitch = true;
            if (pc == doshutdown_va) entered_cpu_common = true;
            if (pc == doshutdown_cb) {
                reached_cb = true;
                break;
            }
            cpu.step(mem);
            TEST_ASSERT(!cpu.exception_taken_in_step(), "Unexpected trap in _Shutdown_context -> _cpu_doshutdown");
        }
        TEST_ASSERT(entered_cswitch && entered_cpu_common && reached_cb,
                    "Must execute cswitch.o::_Shutdown_context -> cpu_common.o::_cpu_doshutdown -> BRAA callback");
        TEST_ASSERT(cpu.read_register(0) == processor_arg,
                    "_cpu_doshutdown must pass processor_t in X0");
        TEST_ASSERT(cpu.get_stack_pointer() == t.int_stack_top,
                    "_Shutdown_context must switch to AUTDA-authenticated int_stack_top");

        // Negative: wrong discriminator on callback must trap EC=0x1C inside cpu_common.o::_cpu_doshutdown (BRAA X2, X16)!
        cpu.set_tpidr_el1(t.thread_va);
        cpu.set_stack_pointer(t.saved_sp);
        cpu.write_register(0, cpu.get_pac().sign_pointer(doshutdown_cb, 0x9999ULL, PacKeyType::InstructionA));
        cpu.write_register(1, processor_arg);
        cpu.write_register(30, t.return_va);
        cpu.set_program_counter(shutdown_va);
        bool trapped_braa = false;
        for (int s = 0; s < 100; ++s) {
            cpu.step(mem);
            if (cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu) {
                trapped_braa = true;
                break;
            }
        }
        TEST_ASSERT(trapped_braa, "Wrong BRAA discriminator in genuine cpu_common.o::_cpu_doshutdown must trap EC=0x1C");
    }

    // 2. Full 3-Object Chain: _Idle_context (cswitch.o) -> _cpu_idle (cpu.o) -> _current_processor (cpu_common.o) ->
    // _idle_enable GOT (cpu_common.o) -> _Idle_load_context (cswitch.o) -> RETAA back to caller!
    {
        CPU cpu;
        init_pac_cpu(cpu, keys);

        ThreadCtx t;
        t.thread_va     = 0x00063000ULL;
        t.cpu_data_va   = 0x00067000ULL;
        t.processor_va  = 0x0005D000ULL; // [thread + 0x1B0] + _percpu_slot_processor (0x10000) == 0x6D000
        t.kstack_va     = 0x0006B000ULL;
        t.int_stack_top = 0x00076000ULL;
        t.saved_sp      = 0x00073000ULL;
        t.return_va     = 0x00086000ULL;
        t.user_tp       = 0x0000000100007777ULL;
        t.cthread_self  = 0x0000000100008888ULL;
        t.saved_ssbs    = true;
        t.saved_fpcr    = 0x01000000u;
        t.reg_seed      = 0xDD00000000ULL;
        install_thread_ctx(mem, cpu, t);

        const std::uint64_t actual_processor_va = 0x0006D000ULL;
        // Set _idle_enable = 0 in staged __DATA,__common (forces cpu_idle -> Idle_load_context without WFE stubs)
        mem.write_u32(static_cast<std::size_t>(idle_enable_va), 0u);
        mem.write_u8(static_cast<std::size_t>(actual_processor_va + 0x7Bu), 1u); // next_idle_short cleared by cpu_idle!
        mem.write_u32(t.return_va, 0xD503201Fu);

        cpu.set_tpidr_el1(t.thread_va);
        cpu.set_stack_pointer(t.saved_sp);
        cpu.set_pstate_ssbs(true);
        cpu.set_fpcr(0x01000000u);
        for (std::uint64_t r = 19; r <= 29; ++r) {
            cpu.write_register(r, 0x7777000000ULL + r);
        }
        cpu.write_register(30, t.return_va);
        cpu.set_program_counter(idle_ctx_va);

        bool saw_idle_ctx = false;
        bool saw_cpu_idle = false;
        bool saw_cur_proc = false;
        bool saw_idle_load = false;
        bool saw_arch_cur_thread = false;
        bool returned_to_caller = false;

        for (int s = 0; s < 200; ++s) {
            const std::uint64_t pc = cpu.get_program_counter();
            if (pc == idle_ctx_va) saw_idle_ctx = true;
            if (pc == cpu_idle_va) saw_cpu_idle = true;
            if (pc == cur_proc_va) saw_cur_proc = true;
            if (pc == idle_load_va) saw_idle_load = true;
            if (linked.classify_va(pc) == SymbolProvenanceClassification::WinIOSArchitectural) {
                saw_arch_cur_thread = true;
            }
            cpu.step(mem);
            TEST_ASSERT(!cpu.exception_taken_in_step(), "Unexpected exception in 3-object idle chain");
            if (cpu.get_program_counter() == t.return_va) {
                returned_to_caller = true;
                break;
            }
        }
        TEST_ASSERT(saw_idle_ctx && saw_cpu_idle && saw_cur_proc && saw_idle_load &&
                    saw_arch_cur_thread && returned_to_caller,
                    "Must traverse cswitch.o::_Idle_context -> cpu.o::_cpu_idle -> cpu_common.o::_current_processor -> cswitch.o::_Idle_load_context -> RETAA");
        TEST_ASSERT(mem.read_u8(static_cast<std::size_t>(actual_processor_va + 0x7Bu)) == 0u,
                    "Genuine cpu.o::_cpu_idle must clear processor->next_idle_short");
        TEST_ASSERT(cpu.get_stack_pointer() == t.saved_sp && cpu.read_register(0) == t.thread_va &&
                    cpu.get_pstate_ssbs(),
                    "Idle round-trip must restore SP, X0=thread_va, SSBS=1 via PACIASP/RETAA");
        for (std::uint64_t r = 19; r <= 29; ++r) {
            TEST_ASSERT(cpu.read_register(r) == 0x7777000000ULL + r,
                        "Idle round-trip must preserve callee-saved registers");
        }
    }

    return 0;
}

int test_call_continuation_leaf_routines_and_smp_replay(const MachOMultiObjectLinkResult& linked,
                                                        Memory& mem,
                                                        const PacKeyRing& keys) {
    // 1. Genuine _ffs (loose_ends.o) & _ml_default_jop_pid (machine_routines_apple.o)
    {
        std::uint64_t ffs_va = 0, jop_va = 0;
        TEST_ASSERT(linked.find_symbol_va("_ffs", ffs_va) &&
                    linked.find_symbol_va("_ml_default_jop_pid", jop_va),
                    "Find _ffs and _ml_default_jop_pid");
        CPU cpu;
        init_pac_cpu(cpu, keys);
        cpu.write_register(0, 0x00100000ULL); // bit 20 -> 1-indexed = 21
        cpu.write_register(30, 0x00088000ULL);
        cpu.set_program_counter(ffs_va);
        for (int s = 0; s < 10 && cpu.get_program_counter() != 0x00088000ULL; ++s) {
            cpu.step(mem);
        }
        TEST_ASSERT(cpu.get_program_counter() == 0x00088000ULL && cpu.read_register(0) == 21ULL,
                    "Genuine loose_ends.o::_ffs(0x100000) must return 21");

        cpu.write_register(30, 0x00088000ULL);
        cpu.set_program_counter(jop_va);
        for (int s = 0; s < 5 && cpu.get_program_counter() != 0x00088000ULL; ++s) {
            cpu.step(mem);
        }
        TEST_ASSERT(cpu.get_program_counter() == 0x00088000ULL && cpu.read_register(0) == 0ULL,
                    "Genuine machine_routines_apple.o::_ml_default_jop_pid must return 0");
    }

    // 2. Honest boundary report for _Call_continuation(x3=1): MIG blocks _ml_set_interrupts_enabled & _thread_terminate
    {
        std::uint64_t cont_va = 0;
        TEST_ASSERT(linked.find_symbol_va("_Call_continuation", cont_va), "Find _Call_continuation");
        CPU cpu;
        init_pac_cpu(cpu, keys);
        ThreadCtx t;
        t.thread_va = 0x00060000ULL;
        t.kstack_va = 0x00068000ULL;
        t.cpu_data_va = 0x00064000ULL;
        install_thread_ctx(mem, cpu, t);
        cpu.set_tpidr_el1(t.thread_va);
        cpu.write_register(0, cpu.get_pac().sign_pointer(0x88000ULL, 0xD507ULL, PacKeyType::InstructionA));
        cpu.write_register(3, 1ULL);
        cpu.set_program_counter(cont_va);

        bool blocked_at_unpatched = false;
        for (int s = 0; s < 40; ++s) {
            const std::uint64_t before = cpu.get_program_counter();
            cpu.step(mem);
            if (cpu.get_program_counter() == before) {
                blocked_at_unpatched = true;
                break;
            }
        }
        TEST_ASSERT(blocked_at_unpatched,
                    "_Call_continuation(x3=1) without MIG stubs must halt honestly at unpatched _ml_set_interrupts_enabled BL");
    }

    // 3. SMP core PAC/SSBS isolation & mid-execution snapshot determinism
    {
        SMPCluster cluster(2, 0x20000ULL);
        auto& smem = cluster.get_memory();
        const std::uint64_t entry0 = 0x1000u, entry1 = 0x2000u;
        // Core 0: PACIASP; AUTIASP; NOP
        smem.write_u32(entry0 + 0u, 0xD503233Fu);
        smem.write_u32(entry0 + 4u, 0xD50323BFu);
        smem.write_u32(entry0 + 8u, 0xD503201Fu);
        // Core 1: PACIBSP; AUTIBSP; NOP
        smem.write_u32(entry1 + 0u, 0xD503237Fu);
        smem.write_u32(entry1 + 4u, 0xD50323FFu);
        smem.write_u32(entry1 + 8u, 0xD503201Fu);

        cluster.initialize_core(0, entry0);
        cluster.start_secondary_core(1, entry1);

        CPU& c0 = cluster.get_core(0);
        CPU& c1 = cluster.get_core(1);
        c0.set_current_el(1);
        c0.set_sp_sel(true);
        c0.set_architectural_exceptions(true);
        c0.set_pac_keyring(keys);
        c0.get_pac().set_fault_mode(PacFaultMode::TrapException);
        c0.set_sctlr_el1(1ULL << 31u);
        c0.set_pstate_ssbs(true);
        c0.set_sp_el1(0x8000u);
        c0.write_register(30, 0x4000u);

        PacKeyRing k1 = keys;
        k1.apib.lo ^= 0xFF00FF00ULL;
        c1.set_current_el(1);
        c1.set_sp_sel(true);
        c1.set_architectural_exceptions(true);
        c1.set_pac_keyring(k1);
        c1.get_pac().set_fault_mode(PacFaultMode::TrapException);
        c1.set_sctlr_el1(1ULL << 30u);
        c1.set_pstate_ssbs(false);
        c1.set_sp_el1(0x9000u);
        c1.write_register(30, 0x5000u);

        // Step once (after PACIASP / PACIBSP), capture full SMP snapshot, then continue & compare!
        (void)cluster.step_round_robin();
        const std::uint64_t signed0 = c0.read_register(30);
        const std::uint64_t signed1 = c1.read_register(30);
        TEST_ASSERT(signed0 != 0x4000u && signed1 != 0x5000u && signed0 != signed1,
                    "Two SMP cores must isolate live PAC signatures & keys");

        SmpCoreState s0, s1;
        s0.capture_from_cpu(c0);
        s1.capture_from_cpu(c1);
        const auto buf0 = serialize_core_architectural_state(s0);
        const auto buf1 = serialize_core_architectural_state(s1);
        TEST_ASSERT(buf0.size() == 1408u && buf1.size() == 1408u, "SMP buffers must be 1408 B");

        (void)cluster.step_round_robin();
        TEST_ASSERT(c0.read_register(30) == 0x4000u && c1.read_register(30) == 0x5000u,
                    "Both cores must authenticate cleanly after resume");

        // Restore mid-execution state into fresh cluster & verify exact deterministic replay hash!
        SmpCoreState rs0, rs1;
        TEST_ASSERT(deserialize_core_architectural_state(buf0.data(), buf0.size(), rs0).success &&
                    deserialize_core_architectural_state(buf1.data(), buf1.size(), rs1).success,
                    "SMP deserialize must succeed");
        CPU rc0, rc1;
        rs0.apply_to_cpu(rc0);
        rs1.apply_to_cpu(rc1);
        rc0.step(smem);
        rc1.step(smem);
        TEST_ASSERT(ReplayStateHash::compute_state_hashes(rc0, smem) ==
                        ReplayStateHash::compute_state_hashes(c0, smem) &&
                    ReplayStateHash::compute_state_hashes(rc1, smem) ==
                        ReplayStateHash::compute_state_hashes(c1, smem),
                    "Restored SMP cores must produce bit-identical replay hashes after AUTIASP/AUTIBSP");
    }

    return 0;
}

} // namespace

int main() {
    try {
        const fs::path p_cswitch = find_existing({
            "D:/WinISO26/xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o",
            "../../xnu_probe_isolated/gen_build/RELEASE_ARM64_OUT/cswitch.o"});
        const fs::path p_common = find_existing({
            "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o",
            "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu_common.o"});
        const fs::path p_cpu = find_existing({
            "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o",
            "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/cpu.o"});
        const fs::path p_loose = find_existing({
            "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/loose_ends.o",
            "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/loose_ends.o"});
        const fs::path p_apple = find_existing({
            "D:/WinISO26/xnu_probe_isolated/gen_build/C282_XNU_DEPS/machine_routines_apple.o",
            "../../xnu_probe_isolated/gen_build/C282_XNU_DEPS/machine_routines_apple.o"});

        TEST_ASSERT(!p_cswitch.empty() && !p_common.empty() && !p_cpu.empty() &&
                    !p_loose.empty() && !p_apple.empty(),
                    "Missing genuine upstream XNU object files");

        const std::vector<MachOObjectInput> inputs = {
            {"xnu-10002.1.13/osfmk/arm64/cswitch.o", MachOObjectIntegrator::parse_object_file(p_cswitch.string())},
            {"xnu-10002.1.13/osfmk/arm/cpu_common.o", MachOObjectIntegrator::parse_object_file(p_common.string())},
            {"xnu-10002.1.13/osfmk/arm64/cpu.o", MachOObjectIntegrator::parse_object_file(p_cpu.string())},
            {"xnu-10002.1.13/osfmk/arm64/loose_ends.o", MachOObjectIntegrator::parse_object_file(p_loose.string())},
            {"xnu-10002.1.13/osfmk/arm/machine_routines_apple.o", MachOObjectIntegrator::parse_object_file(p_apple.string())}
        };

        MachOMultiObjectOptions opts;
        opts.text_base_va      = 0x00004000ULL;
        opts.rodata_base_va    = 0x00020000ULL;
        opts.data_base_va      = 0x00040000ULL;
        opts.stub_base_va      = 0x0005C000ULL;
        opts.virtual_base_bias = 0x0ULL;
        opts.require_all_relocations_resolved = false;
        // Architectural bindings for _current_thread (MRS X0, TPIDR_EL1; RET) and _percpu_slot_processor (0x10000)
        opts.external_bindings = {
            {"_current_thread", SymbolProvenanceClassification::WinIOSArchitectural,
             {0xD538D080u, 0xD65F03C0u}, 0ULL, "TPIDR_EL1 thread pointer getter"},
            {"_percpu_slot_processor", SymbolProvenanceClassification::WinIOSArchitectural,
             {}, 0x00010000ULL, "Prebound percpu slot offset"}
        };

        Memory mem(0x100000ULL);
        const auto linked = MachOObjectIntegrator::stage_objects_in_memory(inputs, opts, mem);
        TEST_ASSERT(linked.is_success() && !linked.has_test_harness_stubs &&
                    linked.has_winios_architectural_bindings,
                    "Multi-object stage must succeed cleanly without test stubs");

        const PacKeyRing keys = default_pac_keys();
        if (test_switch_context_live_paciasp_retaa_roundtrip(linked, mem, keys) != 0) return 1;
        if (test_shutdown_and_idle_cross_object_chains(linked, mem, keys) != 0) return 1;
        if (test_call_continuation_leaf_routines_and_smp_replay(linked, mem, keys) != 0) return 1;

        std::cout << "[PASS] stage282_upstream_pac_execution_tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}
