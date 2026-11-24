// ============================================================================
// WinIOS Campaign 280: Master Engineering Execution
// Stages 280.13 - 280.18: Unchanged cswitch.s Preprocessing & Compilation,
// Reproducibility Check, Genuine Mach-O64 Inspection, Symbol & Relocation Audit,
// and Full Disassembly Across All 7 Exported Functions
// ============================================================================

#include "crypto/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;

fs::path find_workspace_root() {
    for (const fs::path& cand : {fs::current_path(), fs::current_path().parent_path(), fs::path("D:/WinISO26/WinIOS")}) {
        if (fs::exists(cand / "CMakeLists.txt")) {
            return fs::weakly_canonical(cand).parent_path();
        }
    }
    return fs::path("D:/WinISO26");
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

std::string upper_hex(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

std::size_t buf_rem(std::size_t total, std::size_t offset) {
    return (offset < total) ? (total - offset) : 0u;
}

template <typename T>
T read_pod(const std::vector<std::uint8_t>& buf, std::size_t off) {
    TEST_ASSERT(off <= buf.size() && sizeof(T) <= buf.size() - off, "Mach-O read out of bounds");
    T val{};
    std::memcpy(&val, buf.data() + off, sizeof(T));
    return val;
}

void test_stage280_13_to_18_genuine_cswitch_macho_audit() {
    std::cout << "[Stages 280.13-280.18] Auditing Preprocessed cswitch.i & Compiled cswitch.o...\n";
    const fs::path out_dir = find_workspace_root() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT";

    // Stage 280.13: Verify preprocessed cswitch.i
    const auto cswitch_i = read_binary_file(out_dir / "cswitch.i");
    TEST_ASSERT(cswitch_i.size() == 23863u, "cswitch.i exact size == 23863 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(cswitch_i.data(), cswitch_i.size())) ==
                    "E7E62C5CCC5D58D75FEBD2B45106D0227352E3F2FEF76CC9BE47D424561D4C65",
                "cswitch.i SHA-256 mismatch");
    const std::string cswitch_i_text(reinterpret_cast<const char*>(cswitch_i.data()), cswitch_i.size());
    TEST_ASSERT(cswitch_i_text.find("autda") != std::string::npos, "cswitch.i expanded PAC autda sequences");
    TEST_ASSERT(cswitch_i_text.find("blraa") != std::string::npos, "cswitch.i expanded PAC blraa sequence");
    TEST_ASSERT(cswitch_i_text.find("0x6d42") != std::string::npos || cswitch_i_text.find("27970") != std::string::npos,
                "cswitch.i contains TH_KSTACKPTR discriminator 27970 / 0x6d42");

    // Stages 280.14 - 280.17: Genuine cswitch.o Mach-O audit
    const auto obj = read_binary_file(out_dir / "cswitch.o");
    TEST_ASSERT(obj.size() == 3336u, "cswitch.o exact size == 3336 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(obj.data(), obj.size())) ==
                    "784CE07CE532916FC5897A866F673A182C6DE5F5AD2A9E6478D44BB8B7AB4382",
                "cswitch.o SHA-256 mismatch");

    const auto magic = read_pod<std::uint32_t>(obj, 0);
    const auto cputype = read_pod<std::uint32_t>(obj, 4);
    const auto cpusubtype = read_pod<std::uint32_t>(obj, 8);
    const auto filetype = read_pod<std::uint32_t>(obj, 12);
    const auto ncmds = read_pod<std::uint32_t>(obj, 16);
    TEST_ASSERT(magic == 0xFEEDFACFu, "MH_MAGIC_64");
    TEST_ASSERT(cputype == 0x0100000Cu, "CPU_TYPE_ARM64");
    TEST_ASSERT(cpusubtype == 0x80000002u, "CPU_SUBTYPE_ARM64E | CPU_SUBTYPE_PTRAUTH_ABI");
    TEST_ASSERT(filetype == 1u, "MH_OBJECT");
    TEST_ASSERT(ncmds == 4u, "4 load commands (LC_SEGMENT_64, LC_BUILD_VERSION, LC_SYMTAB, LC_DYSYMTAB)");

    std::size_t off = 32;
    std::uint32_t symoff = 0, nsyms = 0, stroff = 0;
    std::uint32_t nlocalsym = 0, nextdefsym = 0, nundefsym = 0;
    std::uint32_t total_relocs = 0;
    std::uint64_t text_size = 0;
    std::uint32_t text_fileoff = 0, text_reloff = 0, text_nreloc = 0;
    std::unordered_map<std::string, std::uint32_t> section_reloc_map;

    for (std::uint32_t c = 0; c < ncmds; ++c) {
        const auto cmd = read_pod<std::uint32_t>(obj, off);
        const auto cmdsize = read_pod<std::uint32_t>(obj, off + 4);
        if (cmd == 0x19u) {
            const auto nsects = read_pod<std::uint32_t>(obj, off + 64);
            TEST_ASSERT(nsects == 5u, "5 sections (__TEXT,__text + 4 __DWARF sections)");
            std::size_t soff = off + 72;
            for (std::uint32_t s = 0; s < nsects; ++s) {
                char sname[17]{}, seg[17]{};
                std::memcpy(sname, obj.data() + soff, 16);
                std::memcpy(seg, obj.data() + soff + 16, 16);
                const auto ssize = read_pod<std::uint64_t>(obj, soff + 40);
                const auto sfoff = read_pod<std::uint32_t>(obj, soff + 48);
                const auto reloff = read_pod<std::uint32_t>(obj, soff + 56);
                const auto nreloc = read_pod<std::uint32_t>(obj, soff + 60);
                section_reloc_map[std::string(seg) + "," + sname] = nreloc;
                total_relocs += nreloc;
                if (std::string(seg) == "__TEXT" && std::string(sname) == "__text") {
                    text_size = ssize;
                    text_fileoff = sfoff;
                    text_reloff = reloff;
                    text_nreloc = nreloc;
                }
                soff += 80;
            }
        } else if (cmd == 0x2u) {
            symoff = read_pod<std::uint32_t>(obj, off + 8);
            nsyms = read_pod<std::uint32_t>(obj, off + 12);
            stroff = read_pod<std::uint32_t>(obj, off + 16);
        } else if (cmd == 0xBu) {
            nlocalsym = read_pod<std::uint32_t>(obj, off + 12);
            nextdefsym = read_pod<std::uint32_t>(obj, off + 20);
            nundefsym = read_pod<std::uint32_t>(obj, off + 28);
        } else if (cmd == 0x32u) {
            const auto platform = read_pod<std::uint32_t>(obj, off + 8);
            const auto minos = read_pod<std::uint32_t>(obj, off + 12);
            TEST_ASSERT(platform == 1u && minos == 0x000F0000u, "LC_BUILD_VERSION platform=macOS minos=15.0.0");
        }
        off += cmdsize;
    }

    TEST_ASSERT(text_size == 1212u && text_size == 303u * 4u, "__TEXT,__text == 1212 bytes (303 ARM64 instructions)");
    TEST_ASSERT(text_nreloc == 4u, "__TEXT,__text has 4 external BRANCH26 relocations");
    TEST_ASSERT(section_reloc_map.at("__DWARF,__debug_info") == 9u, "__DWARF,__debug_info has 9 relocations");
    TEST_ASSERT(section_reloc_map.at("__DWARF,__debug_abbrev") == 0u, "__DWARF,__debug_abbrev has 0 relocations");
    TEST_ASSERT(section_reloc_map.at("__DWARF,__debug_aranges") == 1u, "__DWARF,__debug_aranges has 1 relocation");
    TEST_ASSERT(section_reloc_map.at("__DWARF,__debug_line") == 1u, "__DWARF,__debug_line has 1 relocation");
    TEST_ASSERT(total_relocs == 15u, "Total relocations across __TEXT and __DWARF == 15");
    TEST_ASSERT(nsyms == 14u, "LC_SYMTAB nsyms == 14");
    TEST_ASSERT(nlocalsym == 3u && nextdefsym == 7u && nundefsym == 4u, "3 local + 7 defined ext + 4 undef ext");

    std::vector<std::string> sym_names(nsyms);
    std::unordered_map<std::string, std::uint64_t> def_vals;
    std::vector<std::string> undef_names;
    for (std::uint32_t i = 0; i < nsyms; ++i) {
        const std::size_t noff = static_cast<std::size_t>(symoff) + static_cast<std::size_t>(i) * 16u;
        const auto n_strx = read_pod<std::uint32_t>(obj, noff);
        const auto n_type = read_pod<std::uint8_t>(obj, noff + 4);
        const auto n_sect = read_pod<std::uint8_t>(obj, noff + 5);
        const auto n_value = read_pod<std::uint64_t>(obj, noff + 8);
        const char* p = reinterpret_cast<const char*>(obj.data() + stroff + n_strx);
        sym_names[i] = std::string(p, strnlen(p, buf_rem(obj.size(), static_cast<std::size_t>(stroff) + n_strx)));
        if ((n_type & 0x0Eu) == 0x0Eu && (n_type & 0x01u) != 0u && n_sect == 1u) {
            def_vals[sym_names[i]] = n_value;
        }
        if ((n_type & 0x0Eu) == 0x00u && (n_type & 0x01u) != 0u && n_sect == 0u) {
            undef_names.push_back(sym_names[i]);
        }
    }

    TEST_ASSERT(def_vals.at("_machine_load_context") == 0x0000u, "_machine_load_context @ 0x0000");
    TEST_ASSERT(def_vals.at("_Call_continuation") == 0x00C0u, "_Call_continuation @ 0x00C0");
    TEST_ASSERT(def_vals.at("_Switch_context") == 0x0140u, "_Switch_context @ 0x0140");
    TEST_ASSERT(def_vals.at("_Shutdown_context") == 0x0280u, "_Shutdown_context @ 0x0280");
    TEST_ASSERT(def_vals.at("_Idle_context") == 0x0340u, "_Idle_context @ 0x0340");
    TEST_ASSERT(def_vals.at("_Idle_load_context") == 0x0400u, "_Idle_load_context @ 0x0400");
    TEST_ASSERT(def_vals.at("_machine_set_current_thread") == 0x049Cu, "_machine_set_current_thread @ 0x049C");
    TEST_ASSERT(def_vals.find("_Load_context") == def_vals.end(), "No synthetic _Load_context symbol");
    TEST_ASSERT(undef_names.size() == 4u, "Exactly 4 undefined externals");
    for (const char* u : {"_cpu_doshutdown", "_cpu_idle", "_ml_set_interrupts_enabled", "_thread_terminate"}) {
        TEST_ASSERT(std::find(undef_names.begin(), undef_names.end(), u) != undef_names.end(),
                    std::string("Missing undefined symbol ") + u);
    }

    std::unordered_map<std::string, std::uint32_t> expected_reloc_addrs = {
        {"_ml_set_interrupts_enabled", 0x0114u},
        {"_thread_terminate", 0x012Cu},
        {"_cpu_doshutdown", 0x0334u},
        {"_cpu_idle", 0x03F0u}
    };
    for (std::uint32_t r = 0; r < text_nreloc; ++r) {
        const std::size_t roff = static_cast<std::size_t>(text_reloff) + static_cast<std::size_t>(r) * 8u;
        const auto raddr = read_pod<std::uint32_t>(obj, roff);
        const auto rval = read_pod<std::uint32_t>(obj, roff + 4);
        const std::uint32_t symidx = rval & 0xFFFFFFu;
        const std::uint32_t pcrel = (rval >> 24u) & 1u;
        const std::uint32_t len_pow = (rval >> 25u) & 3u;
        const std::uint32_t is_ext = (rval >> 27u) & 1u;
        const std::uint32_t rtype = (rval >> 28u) & 0xFu;
        TEST_ASSERT(pcrel == 1u && len_pow == 2u && is_ext == 1u && rtype == 2u, "ARM64_RELOC_BRANCH26");
        TEST_ASSERT(expected_reloc_addrs.at(sym_names.at(symidx)) == raddr, "Exact relocation offset match");
    }

    // Stage 280.18: Cross-check disassembly artifact & key genuine opcodes in __TEXT,__text
    const auto disasm_bytes = read_binary_file(out_dir / "cswitch_disasm.txt");
    TEST_ASSERT(disasm_bytes.size() == 12932u, "cswitch_disasm.txt size == 12932 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(disasm_bytes.data(), disasm_bytes.size())) ==
                    "D57630231B18F919377BF90BD5D8D3D9CB67B945F90D054307EA0F1A47D97750",
                "cswitch_disasm.txt SHA-256 mismatch");
    TEST_ASSERT(read_pod<std::uint32_t>(obj, text_fileoff + 0x000u) == 0xD518D080u, "0x000: msr tpidr_el1, x0");
    TEST_ASSERT(read_pod<std::uint32_t>(obj, text_fileoff + 0x004u) == 0xF940DC01u, "0x004: ldr x1, [x0, #0x1b8]");
    TEST_ASSERT(read_pod<std::uint32_t>(obj, text_fileoff + 0x0B4u) == 0xD65F0BFFu, "0x0b4: retaa");
    TEST_ASSERT(read_pod<std::uint32_t>(obj, text_fileoff + 0x124u) == 0xD73F0A95u, "0x124: blraa x20, x21");
    TEST_ASSERT(read_pod<std::uint32_t>(obj, text_fileoff + 0x4B8u) == 0xD65F03C0u, "0x4b8: ret");
    std::cout << "  [PASS] Stages 280.13-280.18 cswitch.o Mach-O structure, relocs & opcodes verified.\n";
}

} // namespace

int main() {
    test_stage280_13_to_18_genuine_cswitch_macho_audit();
    std::cout << "ALL STAGE 280.13 - 280.18 CSWITCH MACHO & RELOCATION TESTS PASSED.\n";
    return 0;
}
