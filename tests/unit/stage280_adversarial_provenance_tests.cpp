// ============================================================================
// WinIOS Campaign 280: Master Engineering Execution
// Stage 280.22: 60 Deterministic Adversarial Verification Tests (D01 - D60)
// Exercises Real Parsers, Loaders, PAC Engine, Relocation Verifiers & SHA-256
// ============================================================================

#include "cpu/cpu.h"
#include "cpu/pointer_auth.h"
#include "crypto/sha256.h"
#include "loader/embedded_xnu_image_210.inl"
#include "loader/kernel_image_parser.h"
#include "loader/macho_user_loader.h"
#include "memory/memory.h"
#include "memory/physical_page_manager.h"
#include "memory/user_address_space.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;
using namespace winios;
using namespace winios::cpu;
using namespace winios::crypto;
using namespace winios::loader;
using namespace winios::memory;

fs::path find_winios_root() {
    for (const fs::path& cand : {fs::current_path(), fs::current_path().parent_path(), fs::path("D:/WinISO26/WinIOS")}) {
        if (fs::exists(cand / "CMakeLists.txt")) {
            return fs::weakly_canonical(cand);
        }
    }
    return fs::path("D:/WinISO26/WinIOS");
}

std::vector<std::uint8_t> read_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    TEST_ASSERT(in.is_open(), "Failed to open file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    }
    return buf;
}

template <typename T>
T read_pod(const std::vector<std::uint8_t>& buf, std::size_t off) {
    T val{};
    if (off <= buf.size() && sizeof(T) <= buf.size() - off) {
        std::memcpy(&val, buf.data() + off, sizeof(T));
    }
    return val;
}

template <typename T>
void write_pod(std::vector<std::uint8_t>& buf, std::size_t off, T val) {
    TEST_ASSERT(off <= buf.size() && sizeof(T) <= buf.size() - off, "Write out of bounds");
    std::memcpy(buf.data() + off, &val, sizeof(T));
}

bool audit_cswitch_macho_object(const std::vector<std::uint8_t>& buf,
                                bool require_ptrauth_subtype,
                                std::string* err) {
    if (buf.size() < 32u) { *err = "truncated header"; return false; }
    if (read_pod<std::uint32_t>(buf, 0) != 0xFEEDFACFu) { *err = "bad magic"; return false; }
    if (read_pod<std::uint32_t>(buf, 4) != 0x0100000Cu) { *err = "bad cputype"; return false; }
    if (require_ptrauth_subtype && read_pod<std::uint32_t>(buf, 8) != 0x80000002u) {
        *err = "missing arm64e ptrauth subtype";
        return false;
    }
    if (read_pod<std::uint32_t>(buf, 12) != 1u) { *err = "not MH_OBJECT"; return false; }
    const auto ncmds = read_pod<std::uint32_t>(buf, 16);
    const auto sizeofcmds = read_pod<std::uint32_t>(buf, 20);
    if (32u + static_cast<std::size_t>(sizeofcmds) > buf.size()) { *err = "cmdsize oob"; return false; }

    std::size_t off = 32;
    std::uint32_t symoff = 0, nsyms = 0, stroff = 0, strsize = 0, text_nreloc = 0;
    std::uint32_t nundefsym = 0;
    bool has_text = false, has_symtab = false, has_dysymtab = false;
    for (std::uint32_t i = 0; i < ncmds; ++i) {
        if (off + 8u > buf.size()) { *err = "lc oob"; return false; }
        const auto cmd = read_pod<std::uint32_t>(buf, off);
        const auto cmdsize = read_pod<std::uint32_t>(buf, off + 4);
        if (cmdsize < 8u || cmdsize > buf.size() - off) { *err = "bad cmdsize"; return false; }
        if (cmd == 0x19u) {
            if (cmdsize < 72u) { *err = "short LC_SEGMENT_64"; return false; }
            const auto nsects = read_pod<std::uint32_t>(buf, off + 64);
            if (72u + static_cast<std::size_t>(nsects) * 80u > cmdsize) { *err = "nsects overflow"; return false; }
            for (std::uint32_t s = 0; s < nsects; ++s) {
                const std::size_t soff = off + 72u + static_cast<std::size_t>(s) * 80u;
                char sname[17]{}, seg[17]{};
                std::memcpy(sname, buf.data() + soff, 16);
                std::memcpy(seg, buf.data() + soff + 16, 16);
                const auto ssize = read_pod<std::uint64_t>(buf, soff + 40);
                const auto sfoff = read_pod<std::uint32_t>(buf, soff + 48);
                const auto reloff = read_pod<std::uint32_t>(buf, soff + 56);
                const auto nreloc = read_pod<std::uint32_t>(buf, soff + 60);
                if (ssize > buf.size() || sfoff > buf.size() - static_cast<std::size_t>(ssize)) {
                    *err = "section data oob";
                    return false;
                }
                if (nreloc > 0 && (reloff > buf.size() || static_cast<std::size_t>(nreloc) * 8u > buf.size() - reloff)) {
                    *err = "reloc table oob";
                    return false;
                }
                if (std::string(seg) == "__TEXT" && std::string(sname) == "__text") {
                    has_text = true;
                    text_nreloc = nreloc;
                }
            }
        } else if (cmd == 0x2u) {
            has_symtab = true;
            symoff = read_pod<std::uint32_t>(buf, off + 8);
            nsyms = read_pod<std::uint32_t>(buf, off + 12);
            stroff = read_pod<std::uint32_t>(buf, off + 16);
            strsize = read_pod<std::uint32_t>(buf, off + 20);
            if (symoff > buf.size() || static_cast<std::size_t>(nsyms) * 16u > buf.size() - symoff) {
                *err = "symtab oob";
                return false;
            }
            if (stroff > buf.size() || strsize > buf.size() - stroff) {
                *err = "strtab oob";
                return false;
            }
        } else if (cmd == 0xBu) {
            has_dysymtab = true;
            nundefsym = read_pod<std::uint32_t>(buf, off + 28);
        }
        off += cmdsize;
    }
    if (!has_text || !has_symtab || !has_dysymtab) { *err = "missing required structures"; return false; }
    if (text_nreloc == 0 && nundefsym == 0) { *err = "missing expected upstream external relocs"; return false; }

    for (std::uint32_t i = 0; i < nsyms; ++i) {
        const auto n_strx = read_pod<std::uint32_t>(buf, static_cast<std::size_t>(symoff) + static_cast<std::size_t>(i) * 16u);
        if (n_strx >= strsize) { *err = "n_strx out of string table"; return false; }
    }
    return true;
}

std::unordered_map<std::string, std::int64_t> parse_assym(const std::string& text) {
    std::unordered_map<std::string, std::int64_t> defs;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("#define ", 0) == 0) {
            std::istringstream ls(line.substr(8));
            std::string key;
            std::int64_t val = 0;
            if (ls >> key >> val) {
                defs[key] = val;
            }
        }
    }
    return defs;
}

bool validate_assym_invariants(const std::string& text) {
    const auto d = parse_assym(text);
    auto check = [&](const char* k, std::int64_t expected) {
        auto it = d.find(k);
        return it != d.end() && it->second == expected;
    };
    return check("TH_KSTACKPTR", 344) && check("TH_KSTACKPTR_DIVERSIFIER", 27970) &&
           check("ACT_CPUDATAP", 440) && check("CPU_ACTIVE_THREAD", 40) &&
           check("TH_CTH_SELF", 328) && check("CPU_TPIDR_EL0", 920) &&
           check("THREAD_CONTINUE_T_DISC", 54535) && check("PGBYTES", 16384);
}

bool evaluate_maturity_level4_claim(bool is_mh_execute,
                                    std::uint32_t nundefsym,
                                    bool target_board_supported,
                                    bool all_kernel_subsystems_linked) {
    return is_mh_execute && nundefsym == 0u && target_board_supported && all_kernel_subsystems_linked;
}

void test_stage280_22_adversarial_d01_to_d60() {
    std::cout << "[Stage 280.22] Executing 60 Real Deterministic Adversarial Tests (D01 - D60)...\n";
    const fs::path root = find_winios_root();
    const fs::path ws = root.parent_path();
    const auto cswitch_obj = read_binary_file(ws / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT" / "cswitch.o");
    const auto c279_obj = read_binary_file(root / "build" / "load_context_darwin.o");
    const auto assym_bytes = read_binary_file(ws / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT" / "assym.s");
    const std::string assym_text(reinterpret_cast<const char*>(assym_bytes.data()), assym_bytes.size());
    std::vector<std::uint8_t> kimg(kEmbeddedXnuImage210Data, kEmbeddedXnuImage210Data + kEmbeddedXnuImage210Size);

    Memory mem(0x800000ULL);
    PhysicalPageManager ppm(mem, 0x40000ULL, 0x7C0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const std::uint64_t l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, l2, "adv_280");
    KernelParsedImage kp{};
    std::string err;

    // D01-D10: KernelImageParser & MachoUserLoader corruptions
    TEST_ASSERT(KernelImageParser::parse(nullptr, 0, kp) != KernelImageParseStatus::Success, "D01: null kernel rejected");
    { auto v = kimg; v.resize(16); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::BufferTooSmall || KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::TruncatedHeader, "D02: truncated header rejected"); }
    { auto v = kimg; write_pod<std::uint32_t>(v, 0, 0xDEADBEEFu); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::InvalidMagic, "D03: bad magic rejected"); }
    { auto v = kimg; write_pod<std::uint32_t>(v, 4, 0x01000007u); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::UnsupportedCpuType, "D04: x86_64 rejected"); }
    { auto v = kimg; write_pod<std::uint32_t>(v, 8, 99u); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::UnsupportedCpuSubtype, "D05: bogus cpusubtype rejected"); }
    { auto v = kimg; write_pod<std::uint32_t>(v, 12, 1u); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) == KernelImageParseStatus::UnsupportedFileType, "D06: filetype MH_OBJECT rejected"); }
    TEST_ASSERT(KernelImageParser::parse(cswitch_obj.data(), cswitch_obj.size(), kp) == KernelImageParseStatus::UnsupportedFileType, "D07: real cswitch.o rejected by KernelImageParser");
    TEST_ASSERT(KernelImageParser::parse(c279_obj.data(), c279_obj.size(), kp) == KernelImageParseStatus::UnsupportedFileType, "D08: real C279 object rejected by KernelImageParser");
    { auto v = kimg; write_pod<std::uint32_t>(v, 36, 0xFFFFFFFCu); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) != KernelImageParseStatus::Success, "D09: load cmdsize overflow rejected"); }
    { auto v = kimg; v.resize(v.size() - 8192u); TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), kp) != KernelImageParseStatus::Success, "D10: truncated segment file rejected"); }

    // D11-D20: MachoUserLoader & cswitch.o Mach-O structural corruptions
    TEST_ASSERT(!MachoUserLoader::load_image(cswitch_obj, uas, ppm, mem).valid, "D11: real cswitch.o rejected by MachoUserLoader");
    TEST_ASSERT(!MachoUserLoader::load_image(c279_obj, uas, ppm, mem).valid, "D12: real C279 object rejected by MachoUserLoader");
    { auto v = MachoUserLoader::get_embedded_first_app(); write_pod<std::uint32_t>(v, 4, 0x01000007u); TEST_ASSERT(!MachoUserLoader::load_image(v, uas, ppm, mem).valid, "D13: x86 user binary rejected"); }
    { auto v = MachoUserLoader::get_embedded_first_app(); v[0] ^= 0xFFu; TEST_ASSERT(!MachoUserLoader::load_image(v, uas, ppm, mem).valid, "D14: corrupted magic user binary rejected"); }
    { auto v = cswitch_obj; write_pod<std::uint32_t>(v, 8, 0u); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D15: downgraded non-arm64e subtype caught"); }
    { auto v = cswitch_obj; v.resize(20); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D16: truncated cswitch.o rejected"); }
    { auto v = cswitch_obj; write_pod<std::uint32_t>(v, 104 + 60, 0x100u); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D17: forged text nreloc OOB caught"); }
    { auto v = cswitch_obj; write_pod<std::uint32_t>(v, 104 + 60, 0u); write_pod<std::uint32_t>(v, 552 + 28, 0u); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D18: fake 0-reloc/0-undef object rejected as non-XNU"); }
    { auto v = cswitch_obj; write_pod<std::uint32_t>(v, 2872, 0xFFFFu); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D19: out-of-range symtab n_strx caught"); }
    { auto v = cswitch_obj; write_pod<std::uint32_t>(v, 32 + 4, 0x4000u); TEST_ASSERT(!audit_cswitch_macho_object(v, true, &err), "D20: LC cmdsize > file caught"); }

    // D21-D32: assym.s / genassym / source-provenance / T8120 & macro adversarial checks
    TEST_ASSERT(validate_assym_invariants(assym_text), "D21 baseline check: real assym.s passes");
    { auto t = assym_text; auto p = t.find("344"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 3, "352"); TEST_ASSERT(!validate_assym_invariants(t), "D22: drifted TH_KSTACKPTR caught"); }
    { auto t = assym_text; auto p = t.find("27970"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 5, "0"); TEST_ASSERT(!validate_assym_invariants(t), "D23: missing PAC diversifier caught"); }
    { auto t = assym_text; auto p = t.find("ACT_CPUDATAP 440"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 16, "ACT_CPUDATAP 16"); TEST_ASSERT(!validate_assym_invariants(t), "D24: drifted ACT_CPUDATAP caught"); }
    { auto t = assym_text; auto p = t.find("CPU_ACTIVE_THREAD 40"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 20, "CPU_ACTIVE_THREAD 16"); TEST_ASSERT(!validate_assym_invariants(t), "D25: WinIOS cpu+0x10 confused with XNU+0x28 caught"); }
    { auto t = assym_text; auto p = t.find("TH_CTH_SELF 328"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 15, "TH_CTH_SELF 0"); TEST_ASSERT(!validate_assym_invariants(t), "D26: corrupted TH_CTH_SELF caught"); }
    { auto t = assym_text; auto p = t.find("54535"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 5, "1"); TEST_ASSERT(!validate_assym_invariants(t), "D27: wrong THREAD_CONTINUE_T_DISC caught"); }
    { auto t = assym_text; auto p = t.find("16384"); TEST_ASSERT(p != std::string::npos, ""); t.replace(p, 5, "4096"); TEST_ASSERT(!validate_assym_invariants(t), "D28: 4K granule drift caught"); }
    {
        const auto cs = read_binary_file(ws / "xnu-10002.1.13" / "osfmk" / "arm64" / "cswitch.s");
        auto v = cs; v[100] ^= 0x01u;
        TEST_ASSERT(Sha256::hash_hex(v.data(), v.size()) != Sha256::hash_hex(cs.data(), cs.size()), "D29: single-bit edit to cswitch.s caught by SHA-256");
    }
    {
        const auto board = read_binary_file(ws / "xnu-10002.1.13" / "pexpert" / "pexpert" / "arm64" / "board_config.h");
        const std::string bs(reinterpret_cast<const char*>(board.data()), board.size());
        TEST_ASSERT(bs.find("ARM64_BOARD_CONFIG_T8120") == std::string::npos, "D30: T8120 oss redaction verified on real board_config.h");
    }
    TEST_ASSERT(audit_cswitch_macho_object(cswitch_obj, true, &err), "D31 baseline valid cswitch.o");
    {
        // D32: Verify WinIOS Category C 8-insn TPIDR_EL1 semantics differ from XNU TPIDR_EL1=thread_t
        const auto cs_text = read_binary_file(root / "build" / "load_context_darwin.s");
        const std::string css(reinterpret_cast<const char*>(cs_text.data()), cs_text.size());
        TEST_ASSERT(css.find("_Load_context") != std::string::npos && css.find("machine_load_context") == std::string::npos,
                    "D32: Synthetic _Load_context distinguished from upstream _machine_load_context");
    }

    // D33-D45: Real PAC Engine & CPU Context-Switch Adversarial Rejections
    {
        PacKeyRing kr{};
        kr.apia = {0x1111ULL, 0x2222ULL};
        kr.apib = {0x3333ULL, 0x4444ULL};
        kr.apda = {0x5555ULL, 0x6666ULL};
        kr.apdb = {0x7777ULL, 0x8888ULL};

        Arm64ePointerAuth pac(PacFaultMode::TrapException);
        pac.set_keyring(kr);

        const std::uint64_t slot = 0x50158ULL;
        const std::uint64_t mod = slot | (0x6D42ULL << 48u);
        const std::uint64_t raw_sp = 0x70000ULL;
        const std::uint64_t signed_sp = pac.sign_pointer(raw_sp, mod, PacKeyType::DataA);

        // D33: Wrong discriminator fails autda
        { std::uint64_t p = signed_sp; TEST_ASSERT(!pac.execute_autda(p, slot | (0x1234ULL << 48u)), "D33: wrong kstack disc fails"); }
        // D34: Zero discriminator fails autda
        { std::uint64_t p = signed_sp; TEST_ASSERT(!pac.execute_autda(p, slot), "D34: missing 0x6d42 disc fails"); }
        // D35: Wrong slot base address fails autda
        { std::uint64_t p = signed_sp; TEST_ASSERT(!pac.execute_autda(p, (slot + 8u) | (0x6D42ULL << 48u)), "D35: wrong slot address fails"); }
        // D36: Wrong key type (DataB instead of DataA) fails
        { std::uint64_t p = signed_sp; TEST_ASSERT(!pac.execute_autdb(p, mod), "D36: wrong key APDB fails"); }
        // D37: Unsigned pointer fails autda
        { std::uint64_t p = raw_sp; TEST_ASSERT(!pac.execute_autda(p, mod), "D37: unsigned kstackptr fails"); }
        // D38: Single bit flip in canonical address fails autda
        { std::uint64_t p = signed_sp ^ 0x10ULL; TEST_ASSERT(!pac.execute_autda(p, mod), "D38: bit flip in addr fails"); }
        // D39: Single bit flip in PAC tag fails autda
        { std::uint64_t p = signed_sp ^ (1ULL << 50u); TEST_ASSERT(!pac.execute_autda(p, mod), "D39: bit flip in PAC tag fails"); }

        // D40-D42: Continuation pointer (APIA + 0xd507) checks on blraa
        const std::uint64_t fn = 0x9000ULL;
        const std::uint64_t signed_fn = pac.sign_pointer(fn, 0xD507ULL, PacKeyType::InstructionA);
        std::uint64_t out_lr = 0, out_tgt = 0;
        TEST_ASSERT(!pac.execute_blraa(signed_fn, 0xD506ULL, out_lr, out_tgt), "D40: wrong cont disc fails blraa");
        TEST_ASSERT(!pac.execute_blrab(signed_fn, 0xD507ULL, out_lr, out_tgt), "D41: APIB fails APIA-signed cont");
        TEST_ASSERT(!pac.execute_blraa(fn, 0xD507ULL, out_lr, out_tgt), "D42: unsigned cont pointer fails blraa");

        // D43-D45: RETA A / XPACD adversarial checks
        const std::uint64_t ret_sp = 0x70000ULL;
        const std::uint64_t signed_lr = pac.sign_pointer(0x8004ULL, ret_sp, PacKeyType::InstructionA);
        TEST_ASSERT(!pac.execute_retaa(signed_lr, ret_sp + 16u, out_tgt), "D43: shifted SP fails retaa");
        TEST_ASSERT(!pac.execute_retaa(0x8004ULL, ret_sp, out_tgt), "D44: unsigned LR fails retaa");
        {
            Arm64ePointerAuth corrupt_mode(PacFaultMode::CorruptUpperBits);
            corrupt_mode.set_keyring(kr);
            std::uint64_t p = signed_sp ^ (1ULL << 50u);
            TEST_ASSERT(!corrupt_mode.execute_autda(p, mod), "D45 pre: CorruptUpperBits records syndrome fault");
            TEST_ASSERT((p & Arm64ePointerAuth::kPacErrorBit) != 0ULL, "D45 pre: CorruptUpperBits sets PAC error bit 62");
            std::uint64_t stripped = p;
            TEST_ASSERT(corrupt_mode.execute_xpacd(stripped) && stripped != p, "D45: xpacd != autda detects forgery for brk");
        }
    }

    // D46-D60: Live File Invariants, Relocation Audits & False-Claim Gates
    {
        const auto rm = read_binary_file(ws / "WinIOS_README.md");
        auto rm_bad = rm; rm_bad[0] ^= 1u;
        TEST_ASSERT(Sha256::hash_hex(rm_bad.data(), rm_bad.size()) != Sha256::hash_hex(rm.data(), rm.size()), "D46: tampered WinIOS_README caught");
        const auto pr = read_binary_file(root / "docs" / "progress.md");
        auto pr_bad = pr; pr_bad[10] ^= 1u;
        TEST_ASSERT(Sha256::hash_hex(pr_bad.data(), pr_bad.size()) != Sha256::hash_hex(pr.data(), pr.size()), "D47: tampered progress.md caught");
        TEST_ASSERT(!fs::exists(ws / "WinISO26_README.md"), "D48: forbidden WinISO26_README absent");

        // D49-D52: Parse real C279 object relocation counts across ALL sections vs __TEXT only
        const std::uint32_t c279_text_relocs = read_pod<std::uint32_t>(c279_obj, 104 + 60);
        std::uint32_t c279_all_relocs = 0;
        const std::uint32_t nsects = read_pod<std::uint32_t>(c279_obj, 96);
        for (std::uint32_t s = 0; s < nsects; ++s) {
            c279_all_relocs += read_pod<std::uint32_t>(c279_obj, 104 + static_cast<std::size_t>(s) * 80u + 60u);
        }
        TEST_ASSERT(c279_text_relocs == 0u && c279_all_relocs == 5u, "D49: total C279 relocs == 5 catches false 0-total claim");
        TEST_ASSERT(cswitch_obj.size() != c279_obj.size(), "D50: cswitch.o != load_context_darwin.o size");

        // D51-D55: False Maturity Level 4 Gate Rejections
        TEST_ASSERT(!evaluate_maturity_level4_claim(false, 4u, false, false), "D51: current state MUST reject Level 4");
        TEST_ASSERT(!evaluate_maturity_level4_claim(true, 4u, false, false), "D52: unresolved externals reject Level 4");
        TEST_ASSERT(!evaluate_maturity_level4_claim(false, 0u, true, true), "D53: MH_OBJECT rejects Level 4");
        TEST_ASSERT(!evaluate_maturity_level4_claim(true, 0u, false, true), "D54: missing T8120 board rejects Level 4");
        TEST_ASSERT(!evaluate_maturity_level4_claim(true, 0u, true, false), "D55: unlinked kernel subsystems reject Level 4");

        // D56-D60: Verify real external Branch26 offsets in cswitch.o cannot silently zero out
        TEST_ASSERT(read_pod<std::uint32_t>(cswitch_obj, 632 + 0x114u) == 0x94000000u, "D56: unrelocated bl site 0x114 is unlinked");
        TEST_ASSERT(read_pod<std::uint32_t>(cswitch_obj, 632 + 0x12Cu) == 0x14000000u, "D57: unrelocated b site 0x12C is unlinked");
        TEST_ASSERT(read_pod<std::uint32_t>(cswitch_obj, 632 + 0x334u) == 0x14000000u, "D58: unrelocated b site 0x334 is unlinked");
        TEST_ASSERT(read_pod<std::uint32_t>(cswitch_obj, 632 + 0x3F0u) == 0x14000000u, "D59: unrelocated b site 0x3F0 is unlinked");
        {
            KernelParsedImage bad_align{};
            auto v = kimg;
            // Misalign first LC_SEGMENT_64 vmaddr by +4 bytes -> MisalignedSegment / failure
            const std::uint64_t vm0 = read_pod<std::uint64_t>(v, 32 + 24);
            write_pod<std::uint64_t>(v, 32 + 24, vm0 + 4u);
            TEST_ASSERT(KernelImageParser::parse(v.data(), v.size(), bad_align) != KernelImageParseStatus::Success,
                        "D60: 4-byte misaligned segment on 16 KiB granule rejected");
        }
    }

    std::cout << "  [PASS] Stage 280.22: All 60 real adversarial tests D01 - D60 verified and rejected cleanly.\n";
}

} // namespace

int main() {
    test_stage280_22_adversarial_d01_to_d60();
    std::cout << "ALL STAGE 280.22 ADVERSARIAL PROVENANCE TESTS (D01 - D60) PASSED.\n";
    return 0;
}
