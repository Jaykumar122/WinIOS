// ============================================================================
// WinIOS Campaign 280: Master Engineering Execution
// Stages 280.0 - 280.6: Baseline & Protected Invariants, C279 Object Audit,
// CMakeLists Investigation, Upstream XNU Acquisition, cswitch.s & T8120 Audit
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
#include <sstream>
#include <string>
#include <vector>

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

namespace {

namespace fs = std::filesystem;

fs::path find_winios_root() {
    for (const fs::path& cand : {fs::current_path(), fs::current_path().parent_path(), fs::path("D:/WinISO26/WinIOS")}) {
        if (fs::exists(cand / "CMakeLists.txt") && fs::exists(cand / "docs" / "progress.md")) {
            return fs::weakly_canonical(cand);
        }
    }
    return fs::path("D:/WinISO26/WinIOS");
}

std::vector<std::uint8_t> read_binary_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    TEST_ASSERT(in.is_open(), "Failed to open binary file: " + p.string());
    in.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    if (sz > 0) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
        TEST_ASSERT(static_cast<std::size_t>(in.gcount()) == sz, "Short read on: " + p.string());
    }
    return buf;
}

std::string read_text_file(const fs::path& p) {
    const auto bytes = read_binary_file(p);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::string upper_hex(std::string s) {
    for (char& ch : s) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return s;
}

template <typename T>
T read_pod(const std::vector<std::uint8_t>& buf, std::size_t off) {
    TEST_ASSERT(off <= buf.size() && sizeof(T) <= buf.size() - off, "Mach-O read out of bounds");
    T val{};
    std::memcpy(&val, buf.data() + off, sizeof(T));
    return val;
}

struct MachoObjectSummary {
    std::uint32_t magic{};
    std::uint32_t cputype{};
    std::uint32_t cpusubtype{};
    std::uint32_t filetype{};
    std::uint32_t ncmds{};
    std::uint32_t text_size{};
    std::uint32_t text_nreloc{};
    std::uint32_t total_nreloc{};
    std::uint32_t nsyms{};
    std::uint32_t nundefsym{};
    std::vector<std::string> symbol_names;
};

MachoObjectSummary inspect_macho_object(const fs::path& p) {
    const auto buf = read_binary_file(p);
    MachoObjectSummary summary{};
    summary.magic = read_pod<std::uint32_t>(buf, 0);
    summary.cputype = read_pod<std::uint32_t>(buf, 4);
    summary.cpusubtype = read_pod<std::uint32_t>(buf, 8);
    summary.filetype = read_pod<std::uint32_t>(buf, 12);
    summary.ncmds = read_pod<std::uint32_t>(buf, 16);

    TEST_ASSERT(summary.magic == 0xFEEDFACFu, "Expected MH_MAGIC_64");
    TEST_ASSERT(summary.filetype == 1u, "Expected MH_OBJECT");

    std::size_t off = 32;
    std::uint32_t symoff = 0;
    std::uint32_t stroff = 0;
    std::uint32_t strsize = 0;
    for (std::uint32_t i = 0; i < summary.ncmds; ++i) {
        const auto cmd = read_pod<std::uint32_t>(buf, off);
        const auto cmdsize = read_pod<std::uint32_t>(buf, off + 4);
        TEST_ASSERT(cmdsize >= 8u && off + cmdsize <= buf.size(), "Valid load command size");
        if (cmd == 0x19u) { // LC_SEGMENT_64
            const auto nsects = read_pod<std::uint32_t>(buf, off + 64);
            std::size_t soff = off + 72;
            for (std::uint32_t s = 0; s < nsects; ++s) {
                char sectname[17]{};
                char segname[17]{};
                std::memcpy(sectname, buf.data() + soff, 16);
                std::memcpy(segname, buf.data() + soff + 16, 16);
                const auto size64 = read_pod<std::uint64_t>(buf, soff + 40);
                const auto nreloc = read_pod<std::uint32_t>(buf, soff + 60);
                summary.total_nreloc += nreloc;
                if (std::string(segname) == "__TEXT" && std::string(sectname) == "__text") {
                    summary.text_size = static_cast<std::uint32_t>(size64);
                    summary.text_nreloc = nreloc;
                }
                soff += 80;
            }
        } else if (cmd == 0x2u) { // LC_SYMTAB
            symoff = read_pod<std::uint32_t>(buf, off + 8);
            summary.nsyms = read_pod<std::uint32_t>(buf, off + 12);
            stroff = read_pod<std::uint32_t>(buf, off + 16);
            strsize = read_pod<std::uint32_t>(buf, off + 20);
        } else if (cmd == 0xBu) { // LC_DYSYMTAB
            summary.nundefsym = read_pod<std::uint32_t>(buf, off + 28);
        }
        off += cmdsize;
    }

    for (std::uint32_t i = 0; i < summary.nsyms; ++i) {
        const std::size_t nlist_off = static_cast<std::size_t>(symoff) + static_cast<std::size_t>(i) * 16u;
        const auto n_strx = read_pod<std::uint32_t>(buf, nlist_off);
        if (n_strx > 0 && n_strx < strsize && static_cast<std::size_t>(stroff) + n_strx < buf.size()) {
            const char* str_ptr = reinterpret_cast<const char*>(buf.data() + stroff + n_strx);
            const std::size_t max_len = buf.size() - (static_cast<std::size_t>(stroff) + n_strx);
            const std::size_t len = strnlen(str_ptr, max_len);
            summary.symbol_names.emplace_back(str_ptr, len);
        }
    }
    return summary;
}

void test_stage280_0_protected_invariants() {
    std::cout << "[Stage 280.0] Verifying Protected Workspace Files & SHA-256 Invariants...\n";
    const fs::path root = find_winios_root();
    const fs::path workspace_root = root.parent_path();

    TEST_ASSERT(!fs::exists(workspace_root / "WinISO26_README.md"),
                "Forbidden WinISO26_README.md must not exist");

    const fs::path readme = workspace_root / "WinIOS_README.md";
    const auto readme_bytes = read_binary_file(readme);
    TEST_ASSERT(readme_bytes.size() == 11054u, "WinIOS_README.md size must remain 11054 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(readme_bytes.data(), readme_bytes.size())) ==
                    "61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF",
                "WinIOS_README.md SHA-256 invariant mismatch");

    const fs::path progress = root / "docs" / "progress.md";
    const auto progress_bytes = read_binary_file(progress);
    TEST_ASSERT(progress_bytes.size() == 22363u, "docs/progress.md size must remain 22363 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(progress_bytes.data(), progress_bytes.size())) ==
                    "447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212",
                "docs/progress.md SHA-256 invariant mismatch");
    std::cout << "  [PASS] Stage 280.0 protected file invariants verified live.\n";
}

void test_stage280_1_c279_artifact_audit() {
    std::cout << "[Stage 280.1] Auditing Campaign 279 Mach-O Object & Relocation Truth...\n";
    const fs::path root = find_winios_root();
    const fs::path c279_s = root / "build" / "load_context_darwin.s";
    const fs::path c279_o = root / "build" / "load_context_darwin.o";

    const auto s_bytes = read_binary_file(c279_s);
    const auto o_bytes = read_binary_file(c279_o);
    TEST_ASSERT(s_bytes.size() == 217u, "load_context_darwin.s exact size == 217 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(s_bytes.data(), s_bytes.size())) ==
                    "66E86105F3EB0035DE36A62391523D60E5654A020E35EC3490F7CA70CCA49ACC",
                "load_context_darwin.s SHA-256 mismatch");
    TEST_ASSERT(o_bytes.size() == 1088u, "load_context_darwin.o exact size == 1088 bytes");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(o_bytes.data(), o_bytes.size())) ==
                    "63AAAAE9BD3417BD070ACB231593F6004C7751FA2FDBF78EE04A08576724ECFF",
                "load_context_darwin.o SHA-256 mismatch");

    const MachoObjectSummary obj = inspect_macho_object(c279_o);
    TEST_ASSERT(obj.cputype == 0x0100000Cu && obj.cpusubtype == 0u, "C279 object is arm64 subtype 0");
    TEST_ASSERT(obj.text_size == 32u, "C279 __TEXT,__text is 32 bytes (8 instructions)");
    TEST_ASSERT(obj.text_nreloc == 0u, "C279 __TEXT,__text nreloc == 0");
    TEST_ASSERT(obj.total_nreloc == 5u, "C279 total Mach-O relocs across __DWARF == 5 (not 0 globally)");
    TEST_ASSERT(obj.nundefsym == 0u, "C279 nundefsym == 0");
    TEST_ASSERT(std::find(obj.symbol_names.begin(), obj.symbol_names.end(), "_Load_context") != obj.symbol_names.end(),
                "C279 exports synthetic _Load_context symbol");
    std::cout << "  [PASS] Stage 280.1 C279 Category C provenance and DWARF reloc audit verified.\n";
}

void test_stage280_2_cmakelists_integrity() {
    std::cout << "[Stage 280.2] Auditing CMakeLists.txt Structure & Registration Integrity...\n";
    const fs::path root = find_winios_root();
    const std::string cmake_text = read_text_file(root / "CMakeLists.txt");
    TEST_ASSERT(cmake_text.find("stage279_adversarial_and_integrity_tests") != std::string::npos,
                "C279 test registration present");
    TEST_ASSERT(cmake_text.find("inferno_integration_tests") != std::string::npos,
                "inferno_integration_tests present in CMakeLists.txt");
    TEST_ASSERT(cmake_text.find("SKIP_RETURN_CODE 77") != std::string::npos,
                "inferno_integration_tests preserves SKIP_RETURN_CODE 77");

    std::size_t add_test_count = 0;
    std::istringstream iss(cmake_text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("add_test(") != std::string::npos) {
            ++add_test_count;
        }
    }
    TEST_ASSERT(add_test_count >= 1230u, "At least 1230 single add_test entries preserved");
    std::cout << "  [PASS] Stage 280.2 CMakeLists.txt count=" << add_test_count << " add_test lines verified.\n";
}

void test_stage280_3_to_6_upstream_xnu_sources() {
    std::cout << "[Stages 280.3-280.6] Verifying Unmodified xnu-10002.1.13 Upstream Source & T8120 Redaction...\n";
    const fs::path xnu_root = find_winios_root().parent_path() / "xnu-10002.1.13";
    TEST_ASSERT(fs::exists(xnu_root / "osfmk" / "arm64" / "cswitch.s"), "Upstream xnu-10002.1.13 source present");

    const auto check_hash = [&](const char* rel, const char* expected_hex, std::size_t expected_sz) {
        const auto bytes = read_binary_file(xnu_root / rel);
        TEST_ASSERT(bytes.size() == expected_sz, std::string("Size mismatch for ") + rel);
        TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(bytes.data(), bytes.size())) == expected_hex,
                    std::string("SHA-256 mismatch for ") + rel);
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };

    const std::string cswitch_s = check_hash("osfmk/arm64/cswitch.s",
        "D95EC6DF06995170AAFD6946073D4B20D4DCC0F281A5F9CF3AFBB6400EF42DEF", 11571u);
    check_hash("osfmk/arm64/genassym.c",
        "BB97784C7B5CBB7F9959CA9B279B92B1516068A6073B8453603CAB5BDC0975FC", 16619u);
    check_hash("osfmk/arm64/asm.h",
        "6B326E30CD2A38642A13B4674B6F427AAE5E2E51DC5E39F90AA7253CF88297CE", 5407u);
    const std::string proc_reg_h = check_hash("osfmk/arm64/proc_reg.h",
        "18B06CC8F1E6EDF76EABF913B7288D6E3C9A53948147738F9CD02B15A617A99F", 104133u);
    const std::string board_cfg_h = check_hash("pexpert/pexpert/arm64/board_config.h",
        "D6B226011C92EFA39FE8835E599AC302C8EFB496EC32DE622CCF14F5452367CC", 4206u);
    check_hash("pexpert/pexpert/arm64/H13.h",
        "BCFC196BEAC9DCC7775B9877C99B84A3D3BE49AFEDDD1CBBA792485C28839F8E", 4307u);

    // Stage 280.5: Confirm exact symbols & dependencies in unmodified cswitch.s
    TEST_ASSERT(cswitch_s.find("#include \"assym.s\"") != std::string::npos, "cswitch.s requires assym.s");
    TEST_ASSERT(cswitch_s.find("Load_context") == std::string::npos, "cswitch.s does NOT define Load_context");
    for (const char* sym : {"machine_load_context", "Call_continuation", "Switch_context",
                            "Shutdown_context", "Idle_context", "Idle_load_context",
                            "machine_set_current_thread"}) {
        TEST_ASSERT(cswitch_s.find(sym) != std::string::npos, std::string("Missing upstream symbol ") + sym);
    }

    // Stage 280.6: Confirm T8120 is redacted in board_config.h & proc_reg.h while T8103 is available
    TEST_ASSERT(board_cfg_h.find("ARM64_BOARD_CONFIG_T8103") != std::string::npos, "T8103 board config defined");
    TEST_ASSERT(board_cfg_h.find("ARM64_BOARD_CONFIG_T8120") == std::string::npos, "T8120 redacted in public OSS board_config.h");
    TEST_ASSERT(proc_reg_h.find("#error processor not supported") != std::string::npos, "Unsupported processor triggers #error in proc_reg.h");
    std::cout << "  [PASS] Stages 280.3-280.6 upstream xnu-10002.1.13 source & T8120 redaction verified.\n";
}

} // namespace

int main() {
    test_stage280_0_protected_invariants();
    test_stage280_1_c279_artifact_audit();
    test_stage280_2_cmakelists_integrity();
    test_stage280_3_to_6_upstream_xnu_sources();
    std::cout << "ALL STAGE 280.0 - 280.6 BASELINE & SOURCE AUDIT TESTS PASSED.\n";
    return 0;
}
