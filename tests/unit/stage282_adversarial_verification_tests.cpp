// ============================================================================
// WinIOS Campaign 282: Adversarial Verification Suite (>= 80 Discrete Checks)
// Stage 282.22: Comprehensive Negative, Fault, Anti-Spoofing & Fidelity Tests
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "cpu/smp_cluster.h"
#include "cpu/smp_core_state.h"
#include "cpu/sysreg.h"
#include "loader/kernel_image_parser.h"
#include "loader/linux_image_loader.h"
#include "loader/macho_loader.h"
#include "loader/macho_object_integrator.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"
#include "replay/replay_state_hash.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_serializer.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace winios;
using namespace winios::cpu;
using namespace winios::loader;
using namespace winios::memory;
using namespace winios::replay;
using namespace winios::snapshot;

int g_pass = 0;
int g_fail = 0;

void check(bool cond, const std::string& label) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::cerr << "[FAIL] " << label << std::endl;
    }
}

constexpr std::uint32_t encode_hint(std::uint32_t imm7) noexcept {
    return 0xD503201Fu | (((imm7 >> 3u) & 0xFu) << 8u) | ((imm7 & 0x7u) << 5u);
}

constexpr std::uint32_t encode_dac1(std::uint32_t subop, std::uint32_t rn, std::uint32_t rd) noexcept {
    return 0xDAC10000u | ((subop & 0x3Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

constexpr std::uint32_t encode_msr_ssbs_imm(std::uint32_t crm) noexcept {
    return 0xD500401Fu | (3u << 16u) | ((crm & 0xFu) << 8u) | (1u << 5u);
}

constexpr std::uint32_t encode_reta(bool key_b) noexcept {
    return 0xD65F0BFFu | (key_b ? (1u << 10u) : 0u);
}

constexpr std::uint32_t encode_bra(std::uint32_t rn, std::uint32_t rm, bool link, bool key_b) noexcept {
    return 0xD71F0800u | (link ? (1u << 21u) : 0u) | (key_b ? (1u << 10u) : 0u) |
           ((rn & 31u) << 5u) | (rm & 31u);
}

PacKeyRing adv_keys() {
    PacKeyRing k{};
    k.apia = {0xCAFE000000000001ULL, 0xCAFE000000000002ULL};
    k.apib = {0xBEEF000000000003ULL, 0xBEEF000000000004ULL};
    k.apda = {0xDEAD000000000005ULL, 0xDEAD000000000006ULL};
    k.apdb = {0xFACE000000000007ULL, 0xFACE000000000008ULL};
    k.apga = {0xFEED000000000009ULL, 0xFEED00000000000AULL};
    return k;
}

fs::path locate_dep(const char* rel_c282) {
    for (const fs::path& base : {fs::path("D:/WinISO26/xnu_probe_isolated/gen_build"),
                                 fs::path("../../xnu_probe_isolated/gen_build")}) {
        const fs::path full = base / rel_c282;
        if (fs::exists(full)) return fs::weakly_canonical(full);
    }
    return {};
}

void run_pac_hint_and_dac_checks() {
    Memory mem(0x20000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x10000ULL);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_pac_keyring(adv_keys());

    // 1..12: All 12 HINT PAC ops execute as NOP when EnIA=0 && EnIB=0 (even on corrupted pointers)
    const std::uint32_t hints[12] = {8u, 10u, 12u, 14u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u};
    cpu.set_sctlr_el1(0ULL);
    for (int i = 0; i < 12; ++i) {
        cpu.set_current_el(1);
        cpu.write_register(16, 0x1111ULL);
        cpu.write_register(17, 0xBAD0000012345000ULL);
        cpu.write_register(30, 0xBAD0000012345000ULL);
        mem.write_u32(0x1000u, encode_hint(hints[i]));
        cpu.set_program_counter(0x1000u);
        cpu.step(mem);
        check(!cpu.exception_taken_in_step() &&
                  cpu.read_register(17) == 0xBAD0000012345000ULL &&
                  cpu.read_register(30) == 0xBAD0000012345000ULL,
              "Adv HINT PAC disabled NOP #" + std::to_string(i));
    }

    // 13..18: EnIA=1 signs & auths IA hints, rejects forged/wrong-SP/wrong-key
    cpu.set_sctlr_el1(1ULL << 31u);
    cpu.set_current_el(1);
    cpu.set_sp_el1(0x12000ULL);
    cpu.write_register(30, 0x0000000000004000ULL);
    mem.write_u32(0x1000u, encode_hint(25u)); // PACIASP
    mem.write_u32(0x1004u, encode_hint(29u)); // AUTIASP
    cpu.set_program_counter(0x1000u);
    cpu.step(mem);
    const std::uint64_t signed_sp = cpu.read_register(30);
    check(signed_sp != 0x4000ULL, "Adv PACIASP signs");
    cpu.set_sp_el1(0x12010ULL); // Mutate SP by 16B -> AUTIASP MUST FAIL!
    cpu.step(mem);
    check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu,
          "Adv AUTIASP detects mutated SP");

    cpu.set_current_el(1);
    cpu.set_sp_el1(0x12000ULL);
    cpu.write_register(30, signed_sp);
    mem.write_u32(0x1008u, encode_hint(28u)); // AUTIAZ (modifier 0 instead of SP -> FAIL!)
    cpu.set_program_counter(0x1008u);
    cpu.step(mem);
    check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu,
          "Adv AUTIAZ rejects PACIASP signature");

    cpu.set_current_el(1);
    cpu.write_register(30, 0x4000ULL);
    mem.write_u32(0x100Cu, encode_hint(27u)); // PACIBSP while only EnIA=1 -> must stay NOP
    cpu.set_program_counter(0x100Cu);
    cpu.step(mem);
    check(cpu.read_register(30) == 0x4000ULL, "Adv EnIA=1 does not enable PACIBSP");

    cpu.set_sctlr_el1(1ULL << 30u); // EnIB=1 only
    cpu.write_register(16, 0xABCDULL);
    cpu.write_register(17, 0x4000ULL);
    mem.write_u32(0x1010u, encode_hint(10u)); // PACIB1716
    mem.write_u32(0x1014u, encode_hint(14u)); // AUTIB1716
    cpu.set_program_counter(0x1010u);
    cpu.step(mem);
    check(cpu.read_register(17) != 0x4000ULL, "Adv PACIB1716 signs with EnIB=1");
    cpu.write_register(16, 0xABCEULL); // flip modifier X16 bit 0
    cpu.step(mem);
    check(cpu.exception_taken_in_step() &&
              ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu &&
              cpu.get_pac().last_syndrome().failed_key == PacKeyType::InstructionB,
          "Adv AUTIB1716 traps with EC=0x1C and InstructionB syndrome on modifier flip");

    // 19..28: All 10 DAC1 zero-mod & strip subops (8..17) reject non-31 Rn
    for (std::uint32_t subop = 8u; subop <= 17u; ++subop) {
        cpu.set_current_el(1);
        mem.write_u32(0x1020u, encode_dac1(subop, 10u, 1u)); // Rn=10 != 31
        cpu.set_program_counter(0x1020u);
        cpu.step(mem);
        check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0u,
              "Adv DAC1 Rn!=31 undef trap subop " + std::to_string(subop));
    }

    // 29..34: Cross-key isolation + TTBR1 (bit 47 == 1) canonical restore + RETAA/BRAA/XPAC traps
    cpu.set_current_el(1);
    cpu.set_sp_el1(0x12000ULL);
    const std::uint64_t ttbr1_ptr = 0xFFFF800012340000ULL;
    const std::uint64_t signed_ttbr1 = cpu.get_pac().sign_pointer(ttbr1_ptr, 0x12000ULL, PacKeyType::InstructionA);
    check(Arm64ePointerAuth::strip_pointer(signed_ttbr1) == ttbr1_ptr,
          "Adv TTBR1 strip restores upper 0xFFFF");
    std::uint64_t tmp = signed_ttbr1;
    check(cpu.get_pac().execute_autia(tmp, 0x12000ULL, 0x1000ULL) && tmp == ttbr1_ptr,
          "Adv AUTIA verifies TTBR1 canonical restoration");
    check(!cpu.get_pac().execute_autib(tmp = signed_ttbr1, 0x12000ULL, 0x1000ULL),
          "Adv AUTIB rejects IA signature on TTBR1");

    // Wrong key on BRAB (X1/X2)
    cpu.write_register(1, cpu.get_pac().sign_pointer(0x5000ULL, 0x77ULL, PacKeyType::InstructionA));
    cpu.write_register(2, 0x77ULL);
    mem.write_u32(0x1030u, encode_bra(1u, 2u, false, true)); // BRAB X1, X2
    cpu.set_program_counter(0x1030u);
    cpu.step(mem);
    check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu,
          "Adv BRAB traps on IA-signed pointer");

    // RETAB fails on IA-signed LR
    cpu.set_current_el(1);
    cpu.write_register(30, cpu.get_pac().sign_pointer(0x5000ULL, 0x12000ULL, PacKeyType::InstructionA));
    mem.write_u32(0x1034u, encode_reta(true));
    cpu.set_program_counter(0x1034u);
    cpu.step(mem);
    check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu,
          "Adv RETAB traps on IA-signed LR");

    // CorruptUpperBits sets bit 62 without trapping on HINT AUTIASP
    cpu.set_current_el(1);
    cpu.set_sctlr_el1(1ULL << 31u);
    cpu.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    cpu.write_register(30, 0xAAAA000012340000ULL);
    mem.write_u32(0x1038u, encode_hint(29u));
    cpu.set_program_counter(0x1038u);
    cpu.step(mem);
    check(!cpu.exception_taken_in_step() &&
              (cpu.read_register(30) & Arm64ePointerAuth::kPacErrorBit) != 0ULL,
          "Adv CorruptUpperBits sets bit 62 on bad AUTIASP");

    // Non-architectural mode throws std::runtime_error on bad RETAA
    cpu.set_architectural_exceptions(false);
    cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);
    cpu.write_register(30, 0xBBBB000012340000ULL);
    mem.write_u32(0x103Cu, encode_reta(false));
    cpu.set_program_counter(0x103Cu);
    bool threw = false;
    try { cpu.step(mem); } catch (const std::runtime_error&) { threw = true; }
    check(threw, "Adv non-architectural RETAA failure throws std::runtime_error");
}

void run_ssbs_and_persistence_checks() {
    Memory mem(0x10000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_vbar_el2(0xA000ULL);
    cpu.set_vbar_el3(0xC000ULL);
    mem.write_u32(0x8400u, 0xD69F03E0u); // ERET
    mem.write_u32(0xA400u, 0xD69F03E0u);
    mem.write_u32(0xC400u, 0xD69F03E0u);

    // 35..44: Reserved MSR SSBS immediates #2..#7 all raise Undefined Instruction trap (EC=0x00)
    for (std::uint32_t imm = 2u; imm <= 7u; ++imm) {
        cpu.set_current_el(1);
        mem.write_u32(0x1000u, encode_msr_ssbs_imm(imm));
        cpu.set_program_counter(0x1000u);
        cpu.step(mem);
        check(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x00u,
              "Adv reserved MSR SSBS imm traps #" + std::to_string(imm));
    }

    // 41..44: SCTLR_EL1.DSSBS=1 sets PSTATE.SSBS=1 on SVC into EL1 even from SSBS=0; ERET restores 0
    cpu.set_current_el(0);
    cpu.set_pstate_ssbs(false);
    cpu.set_sctlr_el1(1ULL << 44u); // DSSBS = 1
    mem.write_u32(0x1010u, 0xD4000001u); // SVC #0
    cpu.set_program_counter(0x1010u);
    cpu.step(mem);
    check(cpu.get_current_el() == 1u && cpu.get_pstate_ssbs(),
          "Adv DSSBS=1 sets PSTATE.SSBS on EL1 entry");
    check(((cpu.get_spsr_el1() >> 12u) & 1u) == 0u,
          "Adv SPSR_EL1[12] preserves EL0 SSBS=0");
    cpu.step(mem); // ERET
    check(cpu.get_current_el() == 0u && !cpu.get_pstate_ssbs(),
          "Adv ERET restores EL0 SSBS=0");

    // 44..54: SmpCoreState & ReplayStateHash sensitivity to all 10 PAC key halves + mode + SSBS
    cpu.reset();
    cpu.set_pac_keyring(adv_keys());
    const auto base_h = ReplayStateHash::compute_state_hashes(cpu, mem);
    SmpCoreState base_smp;
    base_smp.capture_from_cpu(cpu);
    const std::uint64_t smp_base_hash = base_smp.compute_canonical_hash();

    for (int k = 0; k < 5; ++k) {
        for (int half = 0; half < 2; ++half) {
            CPU mod_cpu;
            mod_cpu.reset();
            PacKeyRing mod_kr = adv_keys();
            PacKey128 cur = mod_kr.get_key(static_cast<PacKeyType>(k));
            if (half == 0) cur.lo ^= 1ULL;
            else cur.hi ^= (1ULL << 63u);
            mod_kr.set_key(static_cast<PacKeyType>(k), cur);
            mod_cpu.set_pac_keyring(mod_kr);
            const auto mh = ReplayStateHash::compute_state_hashes(mod_cpu, mem);
            check(mh.system_registers_hash != base_h.system_registers_hash,
                  "Adv ReplayStateHash PAC sensitivity k=" + std::to_string(k) + " half=" + std::to_string(half));

            SmpCoreState mod_smp;
            mod_smp.capture_from_cpu(mod_cpu);
            check(mod_smp.compute_canonical_hash() != smp_base_hash && !(mod_smp == base_smp),
                  "Adv SmpCoreState PAC sensitivity k=" + std::to_string(k) + " half=" + std::to_string(half));
        }
    }

    // 55..62: Snapshot serialization corruption & boundary guards
    cpu.set_pstate_ssbs(true);
    cpu.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    const auto v2_buf = serialize_cpu_state_v2(cpu);
    check(v2_buf.size() == 1224u, "Adv CPU v2 buffer == 1224B");

    CPU sink;
    check(!deserialize_cpu_state(v2_buf.data(), 1127u, sink).success, "Adv reject <1128");
    check(!deserialize_cpu_state(v2_buf.data(), 1130u, sink).success, "Adv reject 1130");
    check(!deserialize_cpu_state(v2_buf.data(), 1137u, sink).success, "Adv reject 1137");
    check(!deserialize_cpu_state(v2_buf.data(), 1223u, sink).success, "Adv reject 1223");
    auto bad_pac2 = v2_buf;
    bad_pac2[1136u] = 0xAAu;
    check(!deserialize_cpu_state(bad_pac2.data(), bad_pac2.size(), sink).success,
          "Adv reject invalid PAC2 magic");
    auto bad_v2mode = v2_buf;
    bad_v2mode[1140u] = 77u;
    check(!deserialize_cpu_state(bad_v2mode.data(), bad_v2mode.size(), sink).success,
          "Adv reject invalid PAC2 fault mode");

    SmpCoreState s;
    s.capture_from_cpu(cpu);
    const auto smp_buf = serialize_core_architectural_state(s);
    SmpCoreState s_out;
    auto bad_smp_magic = smp_buf;
    bad_smp_magic[1321u] ^= 0x55u;
    check(!deserialize_core_architectural_state(bad_smp_magic.data(), bad_smp_magic.size(), s_out).success,
          "Adv reject invalid PACS magic in SMP format");
    auto bad_smp_mode = smp_buf;
    bad_smp_mode[1324u] = 2u;
    check(!deserialize_core_architectural_state(bad_smp_mode.data(), bad_smp_mode.size(), s_out).success,
          "Adv reject invalid PAC mode in SMP format");
}

void run_linker_security_and_fidelity_checks() {
    // 63..74: Relocation encoder boundary/error conditions & malformed object defense
    std::uint32_t insn = 0;
    std::string err;
    check(!MachOObjectIntegrator::encode_branch26(0xD503201Fu, 0x1000u, 0x2000u, insn, err),
          "Adv BRANCH26 rejects NOP");
    check(!MachOObjectIntegrator::encode_branch26(0x94000000u, 0x1002u, 0x2000u, insn, err),
          "Adv BRANCH26 rejects unaligned PC");
    check(!MachOObjectIntegrator::encode_branch26(0x94000000u, 0x1000u, 0x10000000ULL, insn, err),
          "Adv BRANCH26 rejects >128MiB jump");
    check(!MachOObjectIntegrator::encode_page21(0x90000000u, 0x1002u, 0x20000u, insn, err),
          "Adv PAGE21 rejects unaligned PC");
    check(!MachOObjectIntegrator::encode_page21(0x90000000u, 0x200000000ULL, 0x1000u, insn, err),
          "Adv PAGE21 rejects < -4GiB");
    check(!MachOObjectIntegrator::encode_pageoff12(0x71000000u, 0x1000u, insn, err),
          "Adv PAGEOFF12 rejects SUBS imm");
    check(!MachOObjectIntegrator::encode_pageoff12(0x79400000u, 0x1001u, insn, err),
          "Adv PAGEOFF12 rejects odd LDRH offset");
    check(!MachOObjectIntegrator::encode_pageoff12(0xB9400000u, 0x1002u, insn, err),
          "Adv PAGEOFF12 rejects 2-byte aligned LDR W");
    check(!MachOObjectIntegrator::encode_pageoff12(0xF9000000u, 0x1004u, insn, err),
          "Adv PAGEOFF12 rejects 4-byte aligned STR X");
    check(!MachOObjectIntegrator::encode_pageoff12(0xFD400000u, 0x1004u, insn, err),
          "Adv PAGEOFF12 rejects 4-byte aligned LDR D");

    check(!MachOObjectIntegrator::parse_object({}).is_success(), "Adv parse empty rejects");
    check(!MachOObjectIntegrator::parse_object(std::vector<std::uint8_t>(32, 0)).is_success(),
          "Adv parse zero magic rejects");

    // 75..85: Genuine multi-object checks + overlap + spoof + Kernel/User loader MH_OBJECT rejections + Level 3 gate
    const fs::path csw = locate_dep("RELEASE_ARM64_OUT/cswitch.o");
    const fs::path com = locate_dep("C282_XNU_DEPS/cpu_common.o");
    const fs::path cpu = locate_dep("C282_XNU_DEPS/cpu.o");
    check(!csw.empty() && !com.empty() && !cpu.empty(), "Adv XNU objects present");

    const auto p_csw = MachOObjectIntegrator::parse_object_file(csw.string());
    const auto p_com = MachOObjectIntegrator::parse_object_file(com.string());
    const auto p_cpu = MachOObjectIntegrator::parse_object_file(cpu.string());

    // Overlapping segment base VAs rejected (cpu_common.o has non-empty __TEXT and __RODATA)
    MachOMultiObjectOptions overlap_opts;
    overlap_opts.text_base_va   = 0x10000000ULL;
    overlap_opts.rodata_base_va = 0x10000000ULL; // OVERLAPS text_base_va!
    overlap_opts.data_base_va   = 0x10040000ULL;
    overlap_opts.stub_base_va   = 0x10080000ULL;
    const auto overlap_res = MachOObjectIntegrator::link_objects({{"cpu_common.o", p_com}}, overlap_opts);
    check(!overlap_res.is_success() && overlap_res.status == ObjectIntegrationStatus::MemoryBoundsExceeded,
          "Adv overlapping __TEXT/__RODATA segments rejected");

    // Misaligned rodata_base_va rejected
    MachOMultiObjectOptions misalign_opts = overlap_opts;
    misalign_opts.rodata_base_va = 0x10021000ULL; // not 16 KiB aligned
    check(!MachOObjectIntegrator::link_objects({{"cswitch.o", p_csw}}, misalign_opts).is_success(),
          "Adv non-16KiB rodata_base_va rejected");

    // Duplicate strong symbol rejected across two copies of cpu_common.o
    MachOMultiObjectOptions normal_opts;
    normal_opts.text_base_va   = 0x10000000ULL;
    normal_opts.rodata_base_va = 0x10020000ULL;
    normal_opts.data_base_va   = 0x10040000ULL;
    normal_opts.stub_base_va   = 0x10080000ULL;
    const auto dup_res = MachOObjectIntegrator::link_objects(
        {{"c1", p_com}, {"c2", p_com}}, normal_opts);
    check(!dup_res.is_success() && dup_res.status == ObjectIntegrationStatus::MalformedSymbolTable,
          "Adv duplicate strong global symbols rejected");

    // Binding spoofing GenuineUpstream without empty instructions rejected
    MachOMultiObjectOptions spoof_stub = normal_opts;
    spoof_stub.external_bindings = {
        {"_ml_set_interrupts_enabled", SymbolProvenanceClassification::GenuineUpstream, {0xD65F03C0u}, 0ULL, "spoof"}
    };
    check(!MachOObjectIntegrator::link_objects({{"cswitch.o", p_csw}}, spoof_stub).is_success(),
          "Adv TestHarnessStub labeled GenuineUpstream rejected");

    // Binding overriding actual GenuineUpstream symbol (_cpu_idle) rejected
    MachOMultiObjectOptions override_opts = normal_opts;
    override_opts.external_bindings = {
        {"_cpu_idle", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0ULL, "override"}
    };
    check(!MachOObjectIntegrator::link_objects({{"csw", p_csw}, {"cpu", p_cpu}}, override_opts).is_success(),
          "Adv overriding GenuineUpstream _cpu_idle rejected");

    // Empty inputs rejected
    check(!MachOObjectIntegrator::link_objects({}, normal_opts).is_success(),
          "Adv empty multi-object inputs rejected");

    // Verify TestHarnessStub classification stays visibly flagged & distinct (Level 3 fidelity gate, never Level 4)
    MachOMultiObjectOptions mixed_opts = normal_opts;
    mixed_opts.require_all_relocations_resolved = false;
    mixed_opts.external_bindings = {
        {"_ml_set_interrupts_enabled", SymbolProvenanceClassification::TestHarnessStub, {0xD65F03C0u}, 0ULL, "stub"}
    };
    const auto mixed = MachOObjectIntegrator::link_objects(
        {{"csw", p_csw}, {"com", p_com}, {"cpu", p_cpu}}, mixed_opts);
    check(mixed.is_success(), "Adv mixed GenuineUpstream+Stub multi-object link succeeds");
    check(mixed.has_test_harness_stubs && mixed.stub_symbols.size() == 1u,
          "Adv TestHarnessStub explicitly recorded (Level 3 fidelity gate)");
    std::uint64_t doshutdown_va = 0, stub_ml_va = 0;
    check(mixed.find_symbol_va("_cpu_doshutdown", doshutdown_va) &&
              mixed.classify_va(doshutdown_va) == SymbolProvenanceClassification::GenuineUpstream,
          "Adv _cpu_doshutdown classified GenuineUpstream");
    check(mixed.find_symbol_va("_ml_set_interrupts_enabled", stub_ml_va) &&
              mixed.classify_va(stub_ml_va) == SymbolProvenanceClassification::TestHarnessStub,
          "Adv _ml_set_interrupts_enabled classified TestHarnessStub");

    // Memory bounds exceeded in stage_objects_in_memory (512B < 1212B cswitch.o __text)
    Memory tiny_mem(512u);
    check(!MachOObjectIntegrator::stage_objects_in_memory({{"csw", p_csw}}, normal_opts, tiny_mem).is_success(),
          "Adv staging into undersized guest RAM rejected");
}

} // namespace

int main() {
    try {
        run_pac_hint_and_dac_checks();
        run_ssbs_and_persistence_checks();
        run_linker_security_and_fidelity_checks();

        std::cout << "[INFO] stage282_adversarial_verification_tests: passed=" << g_pass
                  << " failed=" << g_fail << std::endl;
        if (g_fail != 0 || g_pass < 75) {
            std::cerr << "[FAIL] Required >= 75 passing adversarial checks with 0 failures" << std::endl;
            return 1;
        }
        std::cout << "[PASS] stage282_adversarial_verification_tests (" << g_pass << " checks)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}
