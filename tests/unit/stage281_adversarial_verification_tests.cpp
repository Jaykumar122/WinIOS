// ============================================================================
// WinIOS Campaign 281: Master Engineering Execution
// Stage 281.22: 64 Deterministic Adversarial Verification Tests (A01 - A64)
// Covers Timer Coalescing, ARM64 FEAT_SSBS, Mach-O MH_OBJECT Relocation & W^X,
// Provenance Anti-Spoofing, PAC Faults, Snapshot Compatibility & Level 4 Guard
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/deterministic_virtual_timer.h"
#include "cpu/pointer_auth.h"
#include "cpu/smp_core_state.h"
#include "cpu/sysreg.h"
#include "loader/kernel_image_parser.h"
#include "loader/macho_loader.h"
#include "loader/macho_object_integrator.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "replay/replay_state_hash.h"
#include "snapshot/snapshot_serializer.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define ADV_CHECK(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] " #cond " -> " << (msg) << " at line " << __LINE__ << std::endl; \
        return 1; \
    } \
    ++g_checks; \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::replay;
using namespace winios::snapshot;
using namespace winios::sysreg;

int g_checks = 0;

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

std::vector<std::uint8_t> read_all(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    const auto sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(buf.data()), sz);
    }
    return buf;
}

std::uint32_t read_u32_le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
         | (static_cast<std::uint32_t>(p[1]) << 8u)
         | (static_cast<std::uint32_t>(p[2]) << 16u)
         | (static_cast<std::uint32_t>(p[3]) << 24u);
}

void write_u32_le(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8u) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16u) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24u) & 0xFFu);
}

int run_adversarial_suite() {
    const fs::path cswitch_path = locate_cswitch_object();
    ADV_CHECK(!cswitch_path.empty(), "A01: cswitch.o located on disk");
    const std::vector<std::uint8_t> valid_obj = read_all(cswitch_path);
    ADV_CHECK(valid_obj.size() == 3336u, "A02: cswitch.o size exact");
    const MachOObjectParseResult parsed_valid = MachOObjectIntegrator::parse_object(valid_obj);
    ADV_CHECK(parsed_valid.is_success(), "A03: parse_object on genuine cswitch.o succeeds");

    // --- Group 1: Timer Adversarial Tests (A04 - A15) ---
    {
        DeterministicVirtualTimer t(24000000ULL);
        ADV_CHECK(t.rearm_periodic(99, 100ULL).status == TimerRearmStatus::InvalidCore,
                  "A04: Out-of-range core rejected");
        ADV_CHECK(t.rearm_periodic(0, 100ULL).status == TimerRearmStatus::Success,
                  "A05: Initial UINT64_MAX comparator rearm initializes relative to counter");
        t.set_control(0, DeterministicVirtualTimer::kCtlEnable);
        t.set_comparator(0, 1000ULL);
        t.advance_counter(1500ULL);
        t.acknowledge(0);
        const auto r0 = t.rearm_periodic(0, 2000ULL);
        ADV_CHECK(r0.status == TimerRearmStatus::Success, "A06: Fixed-rate rearm passes");
        ADV_CHECK(t.get_comparator(0) == 3000ULL, "A07: #1123 deadline == 3000 (not 3500)");
        ADV_CHECK(r0.missed_ticks == 0ULL, "A08: Zero missed ticks when elapsed_periods == 1");
        ADV_CHECK(t.rearm_periodic(0, 0ULL).status == TimerRearmStatus::InvalidPeriod, "A09: Zero period rejected");

        t.set_control(1, DeterministicVirtualTimer::kCtlEnable);
        t.set_comparator(1, 0ULL);
        // counter is currently 1500, so with period=100, n = ((1500-0)/100)+1 = 16 -> next = 1600, missed=15
        const auto r1 = t.rearm_periodic(1, 100ULL);
        ADV_CHECK(r1.status == TimerRearmStatus::Success, "A10: Multi-period catch-up passes");
        ADV_CHECK(r1.next_deadline == 1600ULL, "A11: Exact D + n*P phase after 16 periods");
        ADV_CHECK(r1.missed_ticks == 15ULL, "A12: Coalesced missed ticks == 15");
        ADV_CHECK(t.core_state(2).comparator == (std::numeric_limits<std::uint64_t>::max)(),
                  "A13: Core isolation untouched core 2");
        const auto bad_bytes = std::vector<std::uint8_t>{1u, 2u, 3u};
        ADV_CHECK(!t.deserialize(bad_bytes.data(), bad_bytes.size()),
                  "A14: Corrupted timer snapshot rejected");

        DeterministicVirtualTimer t_ovf(24000000ULL);
        t_ovf.set_control(0, DeterministicVirtualTimer::kCtlEnable);
        t_ovf.set_comparator(0, (std::numeric_limits<std::uint64_t>::max)() - 1ULL);
        t_ovf.advance_counter((std::numeric_limits<std::uint64_t>::max)());
        ADV_CHECK(t_ovf.rearm_periodic(0, 50ULL).status == TimerRearmStatus::DeadlineSaturated,
                  "A15: Uint64 deadline overflow saturates cleanly");
    }

    // --- Group 2: SSBS Adversarial & Architectural Isolation Tests (A16 - A33) ---
    {
        Memory mem(0x10000);
        CPU cpu;
        cpu.reset();
        ADV_CHECK(!cpu.get_pstate_ssbs(), "A16: Reset SSBS == false");

        // All reserved MSR SSBS, #2..#15 must trap cleanly at EL1 without mutating PSTATE.SSBS
        for (std::uint32_t crm = 2u; crm <= 15u; ++crm) {
            const std::uint32_t insn = 0xD500401Fu | (3u << 16u) | (crm << 8u) | (1u << 5u);
            cpu.reset();
            cpu.set_current_el(1);
            cpu.set_sp_sel(true);
            cpu.set_pstate_ssbs(true);
            cpu.set_architectural_exceptions(true);
            cpu.set_vbar_el1(0x4000);
            mem.write_u32(0x1000, insn);
            cpu.set_program_counter(0x1000);
            cpu.step(mem);
            if (!cpu.exception_taken_in_step() || (cpu.get_spsr_el1() & (1u << 12u)) == 0u) {
                std::cerr << "Reserved crm=" << crm << " did not trap properly\n";
                return 1;
            }
        }
        ADV_CHECK(true, "A17: All 14 reserved MSR SSBS, #2..#15 encodings trapped cleanly");

        // Bit-smearing attack: writing every bit EXCEPT bit 12 leaves PSTATE.SSBS == false
        cpu.reset();
        cpu.set_current_el(1);
        cpu.write_register(5, ~(1ULL << 12u));
        mem.write_u32(0x1000, 0xD51B42C5u); // MSR SSBS, X5
        mem.write_u32(0x1004, 0xD53B42C6u); // MRS X6, SSBS
        cpu.set_program_counter(0x1000);
        cpu.step(mem);
        cpu.step(mem);
        ADV_CHECK(!cpu.get_pstate_ssbs(), "A18: Non-bit12 writes do not set SSBS");
        ADV_CHECK(cpu.read_register(6) == 0ULL, "A19: RES0 bits read back zero");

        // Privilege check: SPSel traps at EL0, while SSBS #0/#1 allowed at EL0
        cpu.reset();
        cpu.set_current_el(0);
        cpu.set_architectural_exceptions(true);
        cpu.set_vbar_el1(0x4000);
        mem.write_u32(0x1000, 0xD50041BFu); // MSR SPSel, #1 (privileged)
        cpu.set_program_counter(0x1000);
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 1 && cpu.exception_taken_in_step(), "A20: MSR SPSel trapped at EL0");

        cpu.set_current_el(0);
        mem.write_u32(0x1004, 0xD503413Fu); // MSR SSBS, #1 at EL0
        cpu.set_program_counter(0x1004);
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 0 && cpu.get_pstate_ssbs(), "A21: MSR SSBS, #1 allowed at EL0");

        // SCTLR_EL1.DSSBS=0 clears SSBS on EL1 entry; SPSR_EL1[12] saves old EL0 SSBS=1; ERET restores 1
        cpu.set_sctlr_el1(0ULL);
        mem.write_u32(0x1008, 0xD4000001u); // SVC #0
        mem.write_u32(0x4400, 0xD69F03E0u); // ERET
        cpu.set_program_counter(0x1008);
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 1 && !cpu.get_pstate_ssbs(), "A22: DSSBS=0 clears PSTATE.SSBS on EL1 entry");
        ADV_CHECK((cpu.get_spsr_el1() & (1u << 12u)) != 0u, "A23: SPSR_EL1[12] preserves old SSBS=1");
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 0 && cpu.get_pstate_ssbs(), "A24: ERET restores SSBS=1");

        // SCTLR_EL1.DSSBS=1 sets SSBS=1 on EL1 entry even from SSBS=0 EL0
        cpu.set_pstate_ssbs(false);
        cpu.set_sctlr_el1(1ULL << 44u);
        cpu.set_program_counter(0x1008);
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 1 && cpu.get_pstate_ssbs(), "A25: DSSBS=1 sets SSBS=1 on EL1 entry");
        ADV_CHECK((cpu.get_spsr_el1() & (1u << 12u)) == 0u, "A26: SPSR_EL1[12] captures old SSBS=0");
        cpu.step(mem);
        ADV_CHECK(cpu.get_current_el() == 0 && !cpu.get_pstate_ssbs(), "A27: ERET restores SSBS=0");

        // Snapshot exact byte size & truncation guard
        CPU s0, s1;
        s0.reset();
        s0.set_pstate_ssbs(true);
        auto bytes = serialize_cpu_state(s0);
        ADV_CHECK(bytes.size() == 1128u, "A28: Legacy snapshot payload stays 1128 bytes");
        ADV_CHECK(deserialize_cpu_state(bytes.data(), bytes.size(), s1).success && s1.get_pstate_ssbs(),
                  "A29: Snapshot round-trips SSBS=true");
        auto ext = serialize_cpu_state_with_retirement(s0);
        ADV_CHECK(ext.size() == 1136u, "A30: Extended snapshot stays 1136 bytes");
        s1.set_pstate_ssbs(false);
        ADV_CHECK(!deserialize_cpu_state(ext.data(), 1135u, s1).success && !s1.get_pstate_ssbs(),
                  "A31: Partial 1135B snapshot rejected without state mutation");

        // Core hash isolation
        CPU core0, core1;
        core0.reset(); core1.reset();
        core0.set_core_id(0); core1.set_core_id(1);
        core0.set_pstate_ssbs(true);
        std::uint64_t r0, v0, sh0, r1, v1, sh1;
        ReplayStateHash::hash_cpu(core0, r0, v0, sh0);
        ReplayStateHash::hash_cpu(core1, r1, v1, sh1);
        ADV_CHECK(r0 != r1, "A32: ReplayStateHash regs+PSTATE digest separates SSBS=1 vs SSBS=0");
        winios::SmpCoreState c0, c1;
        c0.capture_from_cpu(core0);
        c1.capture_from_cpu(core1);
        ADV_CHECK(c0.pstate_ssbs && !c1.pstate_ssbs && c0 != c1, "A33: SmpCoreState preserves per-core SSBS isolation");
    }

    // --- Group 3: Mach-O MH_OBJECT Parser, Relocator, Bounds & Provenance Tests (A34 - A55) ---
    {
        ADV_CHECK(!MachOObjectIntegrator::parse_object({}).is_success(), "A34: Empty buffer rejected");
        std::vector<std::uint8_t> bad_magic = valid_obj;
        bad_magic[0] = 0xDEu;
        ADV_CHECK(MachOObjectIntegrator::parse_object(bad_magic).status == ObjectIntegrationStatus::InvalidMagic,
                  "A35: Bad magic rejected");
        std::vector<std::uint8_t> bad_cpu = valid_obj;
        write_u32_le(bad_cpu.data() + 4, 0x01000007u);
        ADV_CHECK(MachOObjectIntegrator::parse_object(bad_cpu).status == ObjectIntegrationStatus::UnsupportedCpuType,
                  "A36: Non-ARM64 cputype rejected");
        std::vector<std::uint8_t> bad_ft = valid_obj;
        write_u32_le(bad_ft.data() + 12, 2u);
        ADV_CHECK(MachOObjectIntegrator::parse_object(bad_ft).status == ObjectIntegrationStatus::NotObjectFile,
                  "A37: Non-MH_OBJECT rejected");
        std::vector<std::uint8_t> trunc_cmds = valid_obj;
        write_u32_le(trunc_cmds.data() + 20, 0x00100000u);
        ADV_CHECK(MachOObjectIntegrator::parse_object(trunc_cmds).status == ObjectIntegrationStatus::CorruptedLoadCommands,
                  "A38: Inflated sizeofcmds rejected");

        std::vector<std::uint8_t> scattered = valid_obj;
        const auto* txt = parsed_valid.find_section("__TEXT", "__text");
        write_u32_le(scattered.data() + txt->reloff, 0x80000000u);
        ADV_CHECK(MachOObjectIntegrator::parse_object(scattered).status ==
                  ObjectIntegrationStatus::UnsupportedRelocationType,
                  "A39: Scattered relocation rejected");

        std::vector<std::uint8_t> rel_oob = valid_obj;
        write_u32_le(rel_oob.data() + txt->reloff, 0x00002000u); // positive OOB offset > 1212 B __text
        const auto p_reloob = MachOObjectIntegrator::parse_object(rel_oob);
        ADV_CHECK(p_reloob.is_success(), "A40: Valid positive r_address parses cleanly before link bounds check");
        MachOIntegrationOptions ok_opts;
        ok_opts.require_all_relocations_resolved = false;
        ADV_CHECK(MachOObjectIntegrator::link_object(p_reloob, ok_opts).status ==
                  ObjectIntegrationStatus::RelocationOutOfBounds,
                  "A41: Out-of-bounds relocation site rejected at link");

        std::vector<std::uint8_t> bad_rtype = valid_obj;
        const std::uint32_t orig_rinfo = read_u32_le(bad_rtype.data() + txt->reloff + 4u);
        write_u32_le(bad_rtype.data() + txt->reloff + 4u, (orig_rinfo & 0x0FFFFFFFu) | (1u << 28u));
        ADV_CHECK(MachOObjectIntegrator::link_object(MachOObjectIntegrator::parse_object(bad_rtype), ok_opts).status ==
                  ObjectIntegrationStatus::UnsupportedRelocationType,
                  "A42: Unsupported relocation type rejected");

        std::vector<std::uint8_t> bad_op = valid_obj;
        write_u32_le(bad_op.data() + txt->offset + 0x114u, 0xD503201Fu);
        MachOIntegrationOptions all_bound = ok_opts;
        all_bound.require_all_relocations_resolved = true;
        all_bound.external_bindings = {
            {"_ml_set_interrupts_enabled", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0, "s"},
            {"_thread_terminate", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0, "s"},
            {"_cpu_doshutdown", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0, "s"},
            {"_cpu_idle", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0, "s"}
        };
        ADV_CHECK(MachOObjectIntegrator::link_object(MachOObjectIntegrator::parse_object(bad_op), all_bound).status ==
                  ObjectIntegrationStatus::RelocationOpcodeMismatch,
                  "A43: BRANCH26 against non-branch opcode rejected");

        std::uint32_t tmp_insn = 0;
        std::string tmp_err;
        ADV_CHECK(MachOObjectIntegrator::encode_branch26(0x94000000u, 0x10000000ULL, 0x10000000ULL + 0x07FFFFFCULL, tmp_insn, tmp_err),
                  "A44: Max +128 MiB - 4 forward branch accepted");
        ADV_CHECK(!MachOObjectIntegrator::encode_branch26(0x94000000u, 0x10000000ULL, 0x10000000ULL + 0x08000000ULL, tmp_insn, tmp_err),
                  "A45: +128 MiB forward overflow rejected");
        ADV_CHECK(MachOObjectIntegrator::encode_branch26(0x14000000u, 0x20000000ULL, 0x20000000ULL - 0x08000000ULL, tmp_insn, tmp_err),
                  "A46: Max -128 MiB backward branch accepted");
        ADV_CHECK(!MachOObjectIntegrator::encode_branch26(0x14000000u, 0x20000000ULL, 0x20000000ULL - 0x08000004ULL, tmp_insn, tmp_err),
                  "A47: -128 MiB - 4 backward underflow rejected");
        ADV_CHECK(!MachOObjectIntegrator::encode_branch26(0x14000000u, 0x10000000ULL, 0x10000002ULL, tmp_insn, tmp_err),
                  "A48: Unaligned BRANCH26 target rejected");

        MachOIntegrationOptions misalign = all_bound;
        misalign.stub_base_va = 0x10004004ULL;
        ADV_CHECK(MachOObjectIntegrator::link_object(parsed_valid, misalign).status ==
                  ObjectIntegrationStatus::MemoryBoundsExceeded,
                  "A49: Non-16KiB-aligned stub_base_va rejected");

        MachOIntegrationOptions overlap = all_bound;
        overlap.stub_base_va = overlap.text_base_va;
        ADV_CHECK(MachOObjectIntegrator::link_object(parsed_valid, overlap).status ==
                  ObjectIntegrationStatus::MemoryBoundsExceeded,
                  "A50: Stub overlapping __TEXT RX page rejected");

        Memory tiny_mem(0x1000);
        all_bound.virtual_base_bias = 0x10000000ULL;
        ADV_CHECK(MachOObjectIntegrator::stage_in_memory(parsed_valid, all_bound, tiny_mem).status ==
                  ObjectIntegrationStatus::MemoryBoundsExceeded,
                  "A51: Staging past tiny Memory capacity rejected");

        MachOIntegrationOptions spoof = all_bound;
        spoof.external_bindings[0].classification = SymbolProvenanceClassification::GenuineUpstream;
        ADV_CHECK(!MachOObjectIntegrator::link_object(parsed_valid, spoof).is_success(),
                  "A52: Provenance spoofing (stub marked GenuineUpstream) rejected");

        MachOIntegrationOptions empty_name = all_bound;
        empty_name.external_bindings.push_back({"", SymbolProvenanceClassification::TestHarnessStub, {}, 0, ""});
        ADV_CHECK(!MachOObjectIntegrator::link_object(parsed_valid, empty_name).is_success(),
                  "A53: Empty external symbol name rejected");

        KernelParsedImage kp_img{};
        ADV_CHECK(KernelImageParser::parse(valid_obj.data(), valid_obj.size(), kp_img) ==
                  KernelImageParseStatus::UnsupportedFileType,
                  "A54: KernelImageParser preserves MH_OBJECT rejection");

        Memory umem(0x800000ULL);
        PhysicalPageManager ppm(umem, 0x40000ULL, 0x7C0000ULL, 0x28121ULL);
        ppm.initialize(false);
        const std::uint64_t l2_pa = ppm.allocate_page();
        UserAddressSpace uas(umem, ppm, l2_pa, "a55_mh_obj");
        ADV_CHECK(!MachoUserLoader::load_image(valid_obj, uas, ppm, umem).valid,
                  "A55: MachoUserLoader preserves MH_OBJECT rejection");
    }

    // --- Group 4: Genuine cswitch.o PAC Tampering & Honest Boundary Tests (A56 - A64) ---
    {
        MachOIntegrationOptions opts;
        opts.text_base_va      = 0x00004000ULL;
        opts.stub_base_va      = 0x00008000ULL;
        opts.virtual_base_bias = 0x0ULL;
        opts.require_all_relocations_resolved = false;

        Memory mem(0x100000ULL);
        const auto staged = MachOObjectIntegrator::stage_in_memory(parsed_valid, opts, mem);
        ADV_CHECK(staged.is_success(), "A56: Stage cswitch.o for PAC adversarial checks");

        std::uint64_t load_ctx_va = 0, cont_va = 0;
        ADV_CHECK(staged.find_symbol_va("_machine_load_context", load_ctx_va) &&
                  staged.find_symbol_va("_Call_continuation", cont_va),
                  "A56b: Resolve _machine_load_context and _Call_continuation");

        PacKeyRing keys{};
        keys.apia = {0xAAAAAAAA11111111ULL, 0x2222222233333333ULL};
        keys.apda = {0x4444444455555555ULL, 0x6666666677777777ULL};

        CPU cpu;
        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);

        const std::uint64_t thread_va = 0x40000ULL;
        const std::uint64_t cpu_data_va = 0x50000ULL;
        const std::uint64_t kstack_va = 0x60000ULL;
        mem.write_u64(thread_va + 0x1B8u, cpu_data_va);
        mem.write_u64(thread_va + 0x158u,
                      cpu.get_pac().sign_pointer(kstack_va, (thread_va + 0x158u) | (0x6D42ULL << 48u), PacKeyType::DataA)
                      ^ (1ULL << 50u));
        cpu.write_register(0, thread_va);
        cpu.set_program_counter(load_ctx_va);
        bool threw = false;
        try {
            for (int i = 0; i < 16; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            threw = true;
        }
        ADV_CHECK(threw && cpu.get_pac().has_fault(), "A57: Corrupted TH_KSTACKPTR PAC trapped in _machine_load_context");

        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_cpacr_el1(3ULL << 20u);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
        mem.write_u64(thread_va + 0x158u,
                      cpu.get_pac().sign_pointer(kstack_va, (thread_va + 0x158u) | (0x6D42ULL << 48u), PacKeyType::DataA));
        mem.write_u64(kstack_va + 0x60u, 0x70000ULL);
        mem.write_u64(kstack_va + 0x58u, 0x80000ULL);
        cpu.write_register(0, thread_va);
        cpu.write_register(1, kstack_va);
        cpu.set_program_counter(load_ctx_va);
        bool retaa_failed = false;
        try {
            for (int i = 0; i < 60; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            retaa_failed = true;
        }
        ADV_CHECK(retaa_failed && cpu.get_pac().has_fault(), "A58: Unsigned LR trapped at genuine 0x0B4 RETAA");

        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_cpacr_el1(3ULL << 20u);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
        mem.write_u64(kstack_va + 0x58u,
                      cpu.get_pac().sign_pointer(0x80000ULL, 0x70010ULL, PacKeyType::InstructionA));
        cpu.write_register(0, thread_va);
        cpu.write_register(1, kstack_va);
        cpu.set_program_counter(load_ctx_va);
        bool wrong_sp_failed = false;
        try {
            for (int i = 0; i < 60; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            wrong_sp_failed = true;
        }
        ADV_CHECK(wrong_sp_failed && cpu.get_pac().has_fault(), "A59: Wrong SP modifier on LR trapped at RETAA");

        cpu.reset();
        cpu.set_current_el(1);
        cpu.set_sp_sel(true);
        cpu.set_pac_keyring(keys);
        cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
        cpu.set_tpidr_el1(thread_va);
        cpu.write_register(0, cpu.get_pac().sign_pointer(0x90000ULL, 0xBEEFULL, PacKeyType::InstructionA));
        cpu.write_register(3, 0ULL);
        cpu.set_program_counter(cont_va);
        bool wrong_cont_failed = false;
        try {
            for (int i = 0; i < 30; ++i) {
                cpu.step(mem);
            }
        } catch (const std::runtime_error&) {
            wrong_cont_failed = true;
        }
        ADV_CHECK(wrong_cont_failed && cpu.get_pac().has_fault(), "A60: Wrong continuation modifier trapped at BLRAA");

        ADV_CHECK(!staged.has_test_harness_stubs, "A61: No-stub staging honestly reports has_test_harness_stubs == false");
        ADV_CHECK(staged.unresolved_symbols.size() == 4u, "A62: No-stub staging tracks all 4 unresolved symbols");
        ADV_CHECK(std::string(symbol_provenance_string(SymbolProvenanceClassification::TestHarnessStub)) !=
                  std::string(symbol_provenance_string(SymbolProvenanceClassification::GenuineUpstream)),
                  "A63: Provenance classifications remain strictly distinct");
        ADV_CHECK(parsed_valid.find_symbol("_ml_set_interrupts_enabled")->is_undefined &&
                  parsed_valid.find_symbol("_thread_terminate")->is_undefined &&
                  parsed_valid.find_symbol("_cpu_doshutdown")->is_undefined &&
                  parsed_valid.find_symbol("_cpu_idle")->is_undefined,
                  "A64: Level 4 rejected — genuine external XNU dependencies remain external to single-TU cswitch.o");
    }

    std::cout << "[PASS] stage281_adversarial_verification_tests (" << g_checks << " checks passed)\n";
    return 0;
}

} // namespace

int main() {
    return run_adversarial_suite();
}
