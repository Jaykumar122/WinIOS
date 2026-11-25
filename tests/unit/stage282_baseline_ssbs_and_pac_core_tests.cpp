// ============================================================================
// WinIOS Campaign 282: Baseline Integrity Forensics, SSBS Audit &
// Genuine ARM64e Pointer Authentication (HINT/Sysreg/Snapshot/Replay)
// Stages 282.0 - 282.9
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "cpu/smp_core_state.h"
#include "cpu/synchronous_exception.h"
#include "cpu/sysreg.h"
#include "loader/linux_image_loader.h"
#include "memory/memory.h"
#include "replay/replay_state_hash.h"
#include "snapshot/smp_snapshot_format.h"
#include "snapshot/snapshot_manager.h"
#include "snapshot/snapshot_restore.h"
#include "snapshot/snapshot_serializer.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
using namespace winios::sysreg;

fs::path locate_repo_root() {
    for (const fs::path& cand : {
             fs::path("D:/WinISO26/WinIOS"),
             fs::path("../.."),
             fs::path(".."),
             fs::path(".")}) {
        const fs::path norm = fs::exists(cand) ? fs::weakly_canonical(cand) : fs::path{};
        if (!norm.empty() &&
            fs::exists(norm.parent_path() / "WinIOS_README.md") &&
            fs::exists(norm / "docs" / "progress.md")) {
            return norm;
        }
    }
    return {};
}

std::string upper_hex(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::vector<std::uint8_t> read_file_bytes(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary | std::ios::ate);
    const auto sz = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(sz));
    if (sz > 0) {
        ifs.read(reinterpret_cast<char*>(out.data()), sz);
    }
    return out;
}

std::string sha256_file_upper(const fs::path& p) {
    const auto buf = read_file_bytes(p);
    return upper_hex(LinuxImageLoader::compute_sha256(buf.data(), buf.size()));
}

constexpr std::uint32_t encode_hint(std::uint32_t imm7) noexcept {
    const std::uint32_t crm = (imm7 >> 3u) & 0xFu;
    const std::uint32_t op2 = imm7 & 0x7u;
    return 0xD503201Fu | (crm << 8u) | (op2 << 5u);
}

constexpr std::uint32_t encode_msr_ssbs_imm(std::uint32_t crm) noexcept {
    return 0xD500401Fu | (3u << 16u) | ((crm & 0xFu) << 8u) | (1u << 5u);
}

constexpr std::uint32_t encode_mrs_ssbs(std::uint32_t rt) noexcept {
    return 0xD53B42C0u | (rt & 31u);
}

constexpr std::uint32_t encode_pac_dac1(std::uint32_t subop, std::uint32_t rn, std::uint32_t rd) noexcept {
    return 0xDAC10000u | ((subop & 0x3Fu) << 10u) | ((rn & 0x1Fu) << 5u) | (rd & 0x1Fu);
}

PacKeyRing test_keys_a() {
    PacKeyRing k{};
    k.apia = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
    k.apib = {0x0102030405060708ULL, 0x1112131415161718ULL};
    k.apda = {0xA5A5A5A55A5A5A5AULL, 0x123456789ABCDEF0ULL};
    k.apdb = {0xCAFEBABEDEADBEEFULL, 0x0F1E2D3C4B5A6978ULL};
    k.apga = {0x7777777777777777ULL, 0x8888888888888888ULL};
    return k;
}

int test_baseline_and_c281_forensics() {
    const fs::path root = locate_repo_root();
    TEST_ASSERT(!root.empty(), "WinIOS repository root must be discoverable");

    TEST_ASSERT(!fs::exists(root.parent_path() / "WinISO26_README.md"),
                "WinISO26_README.md must remain nonexistent");
    TEST_ASSERT(sha256_file_upper(root.parent_path() / "WinIOS_README.md") ==
                    "61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF",
                "WinIOS_README.md protected SHA-256 mismatch");
    TEST_ASSERT(sha256_file_upper(root / "docs" / "progress.md") ==
                    "447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212",
                "docs/progress.md must match actual ...9331CBE... SHA-256");

    const fs::path c281_report = root / "artifacts" / "campaign-281" / "WINIOS_CAMPAIGN_281_FINAL_REPORT.md";
    const fs::path c281_manifest = root / "artifacts" / "campaign-281" / "WINIOS_CAMPAIGN_281_SHA256_MANIFEST.txt";
    TEST_ASSERT(fs::exists(c281_report) && fs::exists(c281_manifest),
                "Campaign 281 closeout artifacts must exist");
    TEST_ASSERT(sha256_file_upper(c281_report) ==
                    "87EB77A39E0C311FDF5E263FE2C34CC55A86EE14476290193C0C2166974FF676",
                "C281 report SHA-256 mismatch");
    TEST_ASSERT(sha256_file_upper(c281_manifest) ==
                    "F7B23004CFE77A2EE3A4ED611FF91F52857E9A8406357AC3AE30354B91DCB5A8",
                "Recovered C281 manifest SHA-256 mismatch");
    return 0;
}

int test_ssbs_multilevel_and_smp_snapshot_fix() {
    Memory mem(0x20000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_vbar_el2(0xA000ULL);
    cpu.set_vbar_el3(0xC000ULL);
    cpu.set_hcr_el2(0ULL);
    cpu.set_scr_el3(0x501ULL); // NS=1, HCE=1 (bit 8), RW=1 (bit 10)
    // Handler at EL1/2/3 sync from lower EL (offset +0x400): ERET
    mem.write_u32(0x8400u, 0xD69F03E0u);
    mem.write_u32(0xA400u, 0xD69F03E0u);
    mem.write_u32(0xC400u, 0xD69F03E0u);

    // 1. EL0 MSR SSBS, #1 / MRS X0, SSBS, then SVC -> EL1 clears SSBS when SCTLR_EL1.DSSBS=0, ERET restores!
    cpu.set_current_el(0);
    cpu.set_sctlr_el1(0ULL); // DSSBS = 0
    mem.write_u32(0x1000u, encode_msr_ssbs_imm(1u));
    mem.write_u32(0x1004u, encode_mrs_ssbs(0u));
    mem.write_u32(0x1008u, 0xD4000001u); // SVC #0
    cpu.set_program_counter(0x1000u);
    cpu.step(mem);
    TEST_ASSERT(cpu.get_pstate_ssbs(), "EL0 MSR SSBS, #1 must set SSBS");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(0) == 0x1000ULL, "EL0 MRS SSBS must read bit 12 (0x1000)");
    cpu.step(mem); // SVC -> EL1
    TEST_ASSERT(cpu.get_current_el() == 1u && !cpu.get_pstate_ssbs(),
                "EL1 entry with DSSBS=0 must clear PSTATE.SSBS");
    TEST_ASSERT(((cpu.get_spsr_el1() >> 12u) & 1u) == 1u,
                "SPSR_EL1[12] must preserve EL0 SSBS=1");
    cpu.step(mem); // ERET back to EL0
    TEST_ASSERT(cpu.get_current_el() == 0u && cpu.get_pstate_ssbs(),
                "ERET from EL1 must restore SSBS=1");

    // 2. EL1 -> EL2 (HVC) and EL1 -> EL3 (SMC) capture SPSR_EL2[12] / SPSR_EL3[12] and restore on ERET.
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_pstate_ssbs(true);
    mem.write_u32(0x2000u, 0xD4000002u); // HVC #0
    mem.write_u32(0x2004u, 0xD4000003u); // SMC #0
    cpu.set_program_counter(0x2000u);
    cpu.step(mem); // HVC -> EL2
    TEST_ASSERT(cpu.get_current_el() == 2u && !cpu.get_pstate_ssbs(),
                "EL2 entry clears PSTATE.SSBS");
    TEST_ASSERT(((cpu.get_spsr_el2() >> 12u) & 1u) == 1u,
                "SPSR_EL2[12] must preserve caller SSBS=1");
    cpu.step(mem); // ERET -> EL1
    TEST_ASSERT(cpu.get_current_el() == 1u && cpu.get_pstate_ssbs(),
                "ERET from EL2 must restore SSBS=1");
    cpu.step(mem); // SMC -> EL3
    TEST_ASSERT(cpu.get_current_el() == 3u && !cpu.get_pstate_ssbs(),
                "EL3 entry clears PSTATE.SSBS");
    TEST_ASSERT(((cpu.get_spsr_el3() >> 12u) & 1u) == 1u,
                "SPSR_EL3[12] must preserve caller SSBS=1");
    cpu.step(mem); // ERET -> EL1
    TEST_ASSERT(cpu.get_current_el() == 1u && cpu.get_pstate_ssbs(),
                "ERET from EL3 must restore SSBS=1");

    // 3. C281 bug fix check: SmpSnapshotFormat serialize_core_architectural_state preserves pstate_ssbs!
    SmpCoreState st;
    st.reset(0, CoreLifecycleState::Runnable);
    st.pstate_ssbs = true;
    const auto raw_smp = serialize_core_architectural_state(st);
    TEST_ASSERT(raw_smp.size() == kCoreArchPayloadSize,
                "SMP core arch payload must equal kCoreArchPayloadSize (1408)");
    SmpCoreState restored_st;
    const auto dres = deserialize_core_architectural_state(raw_smp.data(), raw_smp.size(), restored_st);
    TEST_ASSERT(dres.success && restored_st.pstate_ssbs,
                "SMP core arch deserialize must restore pstate_ssbs=true");

    st.pstate_ssbs = false;
    const auto raw_smp0 = serialize_core_architectural_state(st);
    const auto dres0 = deserialize_core_architectural_state(raw_smp0.data(), raw_smp0.size(), restored_st);
    TEST_ASSERT(dres0.success && !restored_st.pstate_ssbs,
                "SMP core arch deserialize must restore pstate_ssbs=false");

    return 0;
}

int test_hint_pac_disasm_enia_enib_and_traps() {
    Memory mem(0x20000ULL);
    CPU cpu;
    cpu.reset();
    cpu.set_current_el(1);
    cpu.set_sp_sel(true);
    cpu.set_sp_el1(0x18000ULL);
    cpu.set_architectural_exceptions(true);
    cpu.set_vbar_el1(0x8000ULL);
    cpu.set_pac_keyring(test_keys_a());
    cpu.get_pac().set_fault_mode(PacFaultMode::TrapException);

    // Collect trace disassembly for all 12 HINT PAC instructions.
    std::vector<std::string> disasms;
    cpu.set_trace_sink([&](const CPU::TraceRecord& tr) {
        disasms.push_back(tr.disassembly);
    });

    const std::uint32_t hint_imms[12] = {8u, 10u, 12u, 14u, 24u, 25u, 26u, 27u, 28u, 29u, 30u, 31u};
    const char* const hint_names[12] = {
        "PACIA1716", "PACIB1716", "AUTIA1716", "AUTIB1716",
        "PACIAZ", "PACIASP", "PACIBZ", "PACIBSP",
        "AUTIAZ", "AUTIASP", "AUTIBZ", "AUTIBSP"
    };

    // First verify EnIA=0 & EnIB=0 -> HINT PAC instructions behave as NOPs (do NOT mutate forged X17/X30).
    cpu.set_sctlr_el1(0ULL);
    const std::uint64_t forged_ptr = 0xBEEF000012345678ULL;
    for (int i = 0; i < 12; ++i) {
        mem.write_u32(0x1000u + static_cast<std::uint64_t>(i) * 4u, encode_hint(hint_imms[i]));
    }
    cpu.write_register(16, 0x5555ULL);
    cpu.write_register(17, forged_ptr);
    cpu.write_register(30, forged_ptr);
    cpu.set_program_counter(0x1000u);
    for (int i = 0; i < 12; ++i) {
        cpu.step(mem);
        TEST_ASSERT(!cpu.exception_taken_in_step(), "Disabled HINT PAC must not trap");
        TEST_ASSERT(cpu.read_register(17) == forged_ptr && cpu.read_register(30) == forged_ptr,
                    "Disabled HINT PAC must behave as NOP");
    }
    TEST_ASSERT(disasms.size() == 12u, "Expected 12 traced HINT instructions");
    for (std::size_t i = 0; i < 12u; ++i) {
        TEST_ASSERT(disasms[i] == hint_names[i], std::string("Unexpected disasm: ") + disasms[i]);
    }
    cpu.clear_trace_sink();

    // Now enable SCTLR_EL1.EnIA (bit 31) and EnIB (bit 30) and execute genuine sign/auth pairs.
    const std::uint64_t raw_target = 0x0000000000012340ULL;
    const std::uint64_t en_ia = (1ULL << 31u);
    const std::uint64_t en_ib = (1ULL << 30u);

    // Pair 1: PACIASP -> AUTIASP
    cpu.set_sctlr_el1(en_ia);
    cpu.write_register(30, raw_target);
    mem.write_u32(0x2000u, encode_hint(25u)); // PACIASP
    mem.write_u32(0x2004u, encode_hint(29u)); // AUTIASP
    cpu.set_program_counter(0x2000u);
    cpu.step(mem);
    const std::uint64_t signed_iasp = cpu.read_register(30);
    TEST_ASSERT(signed_iasp != raw_target && (signed_iasp >> 48u) != 0u,
                "PACIASP with EnIA=1 must sign X30");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) == raw_target && !cpu.exception_taken_in_step(),
                "AUTIASP must authenticate PACIASP signature cleanly");

    // Pair 2: PACIAZ -> AUTIAZ
    cpu.write_register(30, raw_target);
    mem.write_u32(0x2010u, encode_hint(24u)); // PACIAZ
    mem.write_u32(0x2014u, encode_hint(28u)); // AUTIAZ
    cpu.set_program_counter(0x2010u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) != raw_target, "PACIAZ must sign X30 with mod 0");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) == raw_target, "AUTIAZ must restore X30");

    // Pair 3: PACIA1716 -> AUTIA1716
    cpu.write_register(16, 0xCAFE0001ULL);
    cpu.write_register(17, raw_target);
    mem.write_u32(0x2020u, encode_hint(8u));  // PACIA1716
    mem.write_u32(0x2024u, encode_hint(12u)); // AUTIA1716
    cpu.set_program_counter(0x2020u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(17) != raw_target, "PACIA1716 must sign X17 with X16");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(17) == raw_target, "AUTIA1716 must authenticate X17");

    // Pair 4: IB pairs (PACIBSP/AUTIBSP, PACIBZ/AUTIBZ, PACIB1716/AUTIB1716) require EnIB=1, NOT EnIA!
    cpu.set_sctlr_el1(en_ia); // EnIB=0 -> PACIBSP stays NOP!
    cpu.write_register(30, raw_target);
    mem.write_u32(0x2030u, encode_hint(27u)); // PACIBSP
    cpu.set_program_counter(0x2030u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) == raw_target, "EnIA=1 alone must NOT enable PACIBSP");

    cpu.set_sctlr_el1(en_ib);
    mem.write_u32(0x2040u, encode_hint(27u)); // PACIBSP
    mem.write_u32(0x2044u, encode_hint(31u)); // AUTIBSP
    mem.write_u32(0x2048u, encode_hint(26u)); // PACIBZ
    mem.write_u32(0x204Cu, encode_hint(30u)); // AUTIBZ
    mem.write_u32(0x2050u, encode_hint(10u)); // PACIB1716
    mem.write_u32(0x2054u, encode_hint(14u)); // AUTIB1716
    cpu.set_program_counter(0x2040u);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) != raw_target, "PACIBSP must sign with EnIB=1");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) == raw_target, "AUTIBSP must authenticate with EnIB=1");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) != raw_target, "PACIBZ must sign with EnIB=1");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(30) == raw_target, "AUTIBZ must authenticate with EnIB=1");
    cpu.write_register(17, raw_target);
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(17) != raw_target, "PACIB1716 must sign X17");
    cpu.step(mem);
    TEST_ASSERT(cpu.read_register(17) == raw_target, "AUTIB1716 must authenticate X17");

    // Fault tests: AUTIASP forged pointer -> EC=0x1C TrapException (iss=0 for IA), AUTIBSP -> EC=0x1C (iss=1 for IB)
    cpu.set_sctlr_el1(en_ia | en_ib);
    cpu.write_register(30, forged_ptr);
    mem.write_u32(0x2060u, encode_hint(29u)); // AUTIASP
    cpu.set_program_counter(0x2060u);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step(), "Forged AUTIASP must trap under TrapException");
    TEST_ASSERT(((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu, "ESR_EL1.EC must be 0x1C");
    TEST_ASSERT((cpu.get_esr_el1() & 0x3u) == 0u, "IA failure ISS must be 0");

    cpu.set_current_el(1);
    cpu.write_register(30, forged_ptr);
    mem.write_u32(0x2070u, encode_hint(31u)); // AUTIBSP
    cpu.set_program_counter(0x2070u);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0x1Cu,
                "Forged AUTIBSP must trap with EC=0x1C");
    TEST_ASSERT(cpu.get_pac().last_syndrome().failed_key == PacKeyType::InstructionB,
                "AUTIBSP trap syndrome must record InstructionB");

    // CorruptUpperBits mode: no trap, error bit 62 injected
    cpu.set_current_el(1);
    cpu.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    cpu.write_register(30, forged_ptr);
    mem.write_u32(0x2080u, encode_hint(29u)); // AUTIASP
    cpu.set_program_counter(0x2080u);
    cpu.step(mem);
    TEST_ASSERT(!cpu.exception_taken_in_step(), "CorruptUpperBits must not trap on AUTIASP");
    TEST_ASSERT((cpu.read_register(30) & Arm64ePointerAuth::kPacErrorBit) != 0ULL,
                "CorruptUpperBits must inject error bit 62");

    // DAC1 zero-modifier non-31 Rn must raise Undefined Instruction (EC=0x00)
    mem.write_u32(0x2090u, encode_pac_dac1(8u, 5u, 0u)); // PACIZA with Rn=5 (!=31)
    cpu.set_program_counter(0x2090u);
    cpu.step(mem);
    TEST_ASSERT(cpu.exception_taken_in_step() && ((cpu.get_esr_el1() >> 26u) & 0x3Fu) == 0u,
                "DAC1 PACIZA with Rn!=31 must raise Undefined Instruction trap");

    return 0;
}

int test_pac_reset_replay_and_snapshot_persistence() {
    // 1. CPU::reset() deterministically resets custom PAC keys, fault mode, and fault syndrome
    CPU cpu1;
    CPU cpu2;
    cpu1.set_pac_keyring(test_keys_a());
    cpu1.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    std::uint64_t bad = 0xBAD0000012340000ULL;
    (void)cpu1.get_pac().execute_autia(bad, 0x100ULL, 0x4000ULL);
    TEST_ASSERT(cpu1.get_pac().has_fault(), "Expected recorded PAC fault before reset");
    cpu1.reset();
    cpu2.reset();
    TEST_ASSERT(cpu1.get_pac().get_fault_mode() == PacFaultMode::TrapException &&
                !cpu1.get_pac().has_fault(),
                "CPU::reset must reset PAC fault mode & clear syndrome");
    TEST_ASSERT(cpu1.get_pac_keyring().apia == cpu2.get_pac_keyring().apia &&
                cpu1.get_pac_keyring().apib == cpu2.get_pac_keyring().apib &&
                cpu1.get_pac_keyring().apda == cpu2.get_pac_keyring().apda &&
                cpu1.get_pac_keyring().apdb == cpu2.get_pac_keyring().apdb &&
                cpu1.get_pac_keyring().apga == cpu2.get_pac_keyring().apga,
                "CPU::reset must restore deterministic default keyring");

    // 2. ReplayStateHash sensitivity to PAC keys & fault mode
    Memory mem(0x4000u);
    cpu1.set_pac_keyring(test_keys_a());
    const auto h1 = ReplayStateHash::compute_state_hashes(cpu1, mem);
    PacKeyRing kmod = test_keys_a();
    kmod.apga.hi ^= 1ULL;
    cpu1.set_pac_keyring(kmod);
    const auto h2 = ReplayStateHash::compute_state_hashes(cpu1, mem);
    TEST_ASSERT(h1.system_registers_hash != h2.system_registers_hash,
                "ReplayStateHash must reflect PAC key mutations");
    cpu1.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    const auto h3 = ReplayStateHash::compute_state_hashes(cpu1, mem);
    TEST_ASSERT(h2.system_registers_hash != h3.system_registers_hash,
                "ReplayStateHash must reflect PAC fault mode mutations");

    // 3. SmpCoreState & SmpSnapshotFormat (1408B) PAC + SSBS persistence
    SmpCoreState s1;
    s1.capture_from_cpu(cpu1);
    const std::uint64_t ch1 = s1.compute_canonical_hash();
    s1.pac_keyring.apdb.lo ^= 0x8000ULL;
    TEST_ASSERT(s1.compute_canonical_hash() != ch1,
                "SmpCoreState canonical hash must detect PAC key change");
    const auto smp_bytes = serialize_core_architectural_state(s1);
    TEST_ASSERT(smp_bytes.size() == 1408u, "Serialized SMP core arch payload must be 1408 B");
    SmpCoreState s2;
    TEST_ASSERT(deserialize_core_architectural_state(smp_bytes.data(), smp_bytes.size(), s2).success,
                "SmpSnapshot deserialize v2 must succeed");
    TEST_ASSERT(s1 == s2 && s2.pac_keyring.apdb.lo == s1.pac_keyring.apdb.lo &&
                s2.pac_fault_mode == PacFaultMode::CorruptUpperBits,
                "SMP core arch round-trip must preserve PAC keys & fault mode");

    // Backward compat: 1080B and 1320B buffers still decode cleanly
    const auto res_1080 = deserialize_core_architectural_state(smp_bytes.data(), 1080u, s2);
    const auto res_1320 = deserialize_core_architectural_state(smp_bytes.data(), 1320u, s2);
    TEST_ASSERT(res_1080.success && res_1320.success,
                "Legacy 1080B and 1320B SMP payloads must stay backward compatible");

    // Corrupt magic & fault mode inside 1408B PAC block rejected cleanly
    auto bad_magic = smp_bytes;
    bad_magic[1320u] ^= 0xFFu;
    TEST_ASSERT(!deserialize_core_architectural_state(bad_magic.data(), bad_magic.size(), s2).success,
                "Corrupt PACS magic in SMP payload must fail");
    auto bad_mode = smp_bytes;
    bad_mode[1324u] = 9u;
    TEST_ASSERT(!deserialize_core_architectural_state(bad_mode.data(), bad_mode.size(), s2).success,
                "Invalid PAC fault mode in SMP payload must fail");

    // 4. serialize_cpu_state_v2 (1224B) + legacy 1128B/1136B + SnapshotManager roundtrip
    cpu1.set_pac_keyring(test_keys_a());
    cpu1.set_pstate_ssbs(true);
    cpu1.get_pac().set_fault_mode(PacFaultMode::CorruptUpperBits);
    const auto b1 = serialize_cpu_state(cpu1);
    const auto b_ret = serialize_cpu_state_with_retirement(cpu1);
    const auto b_v2 = serialize_cpu_state_v2(cpu1);
    TEST_ASSERT(b1.size() == 1128u && b_ret.size() == 1136u && b_v2.size() == 1224u,
                "Serialized CPU state sizes must match 1128 / 1136 / 1224");

    CPU restored;
    TEST_ASSERT(deserialize_cpu_state(b_v2.data(), b_v2.size(), restored).success,
                "v2 CPU state deserialize must succeed");
    TEST_ASSERT(restored.get_pac_keyring().apia == cpu1.get_pac_keyring().apia &&
                restored.get_pac_keyring().apga == cpu1.get_pac_keyring().apga &&
                restored.get_pac().get_fault_mode() == PacFaultMode::CorruptUpperBits &&
                restored.get_pstate_ssbs(),
                "CPU v2 restore must preserve all PAC keys, fault mode, and SSBS");

    // Truncated between 1136 and 1224 must fail
    TEST_ASSERT(!deserialize_cpu_state(b_v2.data(), 1200u, restored).success,
                "Truncated v2 CPU buffer must be rejected");

    // Full SnapshotManager capture_to_buffer -> restore_snapshot_from_buffer round-trip
    VirtualTime vt;
    GICv3 gic;
    SnapshotRestoreContext cap_ctx{&cpu1, &mem, &vt, &gic, {}};
    std::vector<std::uint8_t> wsnap;
    TEST_ASSERT(SnapshotManager::capture_to_buffer(cap_ctx, wsnap).success,
                "SnapshotManager capture must succeed");
    CPU restored_full;
    Memory restored_mem(0x4000u);
    VirtualTime restored_vt;
    GICv3 restored_gic;
    SnapshotRestoreContext res_ctx{&restored_full, &restored_mem, &restored_vt, &restored_gic, {}};
    TEST_ASSERT(restore_snapshot_from_buffer(wsnap.data(), wsnap.size(), res_ctx).success,
                "SnapshotManager restore must succeed");
    TEST_ASSERT(restored_full.get_pac_keyring().apia == test_keys_a().apia &&
                restored_full.get_pac_keyring().apga == test_keys_a().apga &&
                restored_full.get_pac().get_fault_mode() == PacFaultMode::CorruptUpperBits &&
                restored_full.get_pstate_ssbs(),
                "Full .wsnap restore must preserve PAC keys, fault mode, and SSBS");

    return 0;
}

} // namespace

int main() {
    try {
        if (test_baseline_and_c281_forensics() != 0) return 1;
        if (test_ssbs_multilevel_and_smp_snapshot_fix() != 0) return 1;
        if (test_hint_pac_disasm_enia_enib_and_traps() != 0) return 1;
        if (test_pac_reset_replay_and_snapshot_persistence() != 0) return 1;
        std::cout << "[PASS] stage282_baseline_ssbs_and_pac_core_tests\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Unhandled exception: " << e.what() << std::endl;
        return 1;
    }
}
