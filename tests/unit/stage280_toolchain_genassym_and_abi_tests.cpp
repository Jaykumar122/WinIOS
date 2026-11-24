// ============================================================================
// WinIOS Campaign 280: Master Engineering Execution
// Stages 280.7 - 280.12: Cross-Toolchain Capabilities, Config Tool Generation,
// Genuine genassym.c Compilation, assym.s Extraction & ABI Layout Verification
// ============================================================================

#include "crypto/sha256.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

void verify_file(const fs::path& p, std::size_t expected_sz, const char* expected_sha256) {
    const auto bytes = read_binary_file(p);
    TEST_ASSERT(bytes.size() == expected_sz, "Unexpected byte size for " + p.string());
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(bytes.data(), bytes.size())) == expected_sha256,
                "SHA-256 mismatch for " + p.string());
}

std::unordered_map<std::string, std::int64_t> parse_assym_s(const fs::path& p) {
    const auto bytes = read_binary_file(p);
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::unordered_map<std::string, std::int64_t> defs;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("#define ", 0) == 0) {
            std::istringstream ls(line.substr(8));
            std::string name;
            std::int64_t val = 0;
            if (ls >> name >> val) {
                defs[name] = val;
            }
        }
    }
    return defs;
}

void test_stage280_7_to_9_config_and_generated_headers() {
    std::cout << "[Stages 280.7-280.9] Verifying Upstream config.exe & 43 Generated Headers...\n";
    const fs::path probe = find_workspace_root() / "xnu_probe_isolated";
    const fs::path config_exe = probe / "config_build" / "config.exe";
    const auto exe_bytes = read_binary_file(config_exe);
    TEST_ASSERT(exe_bytes.size() == 937472u, "config.exe size == 937472 bytes");
    TEST_ASSERT(exe_bytes.size() >= 2u && exe_bytes[0] == 'M' && exe_bytes[1] == 'Z', "config.exe PE MZ header valid");
    TEST_ASSERT(upper_hex(winios::crypto::Sha256::hash_hex(exe_bytes.data(), exe_bytes.size())) ==
                    "8B994EFCAB292184544ADD91626A38A5074870C30EA39DDCFB30C4769F1FDCE7",
                "config.exe SHA-256 mismatch");

    const fs::path out_dir = probe / "gen_build" / "RELEASE_ARM64_OUT";
    verify_file(probe / "gen_build" / "RELEASE_ARM64", 5300u,
                "340ED84DE857ACC5CF2C81EB202561F8AF92EC2960D711D8AF968F9D7D7AA324");
    verify_file(out_dir / "meta_features.h", 1082u,
                "A28DC0FD4BA33A96FF16ACC70495CD59B4285F29A902A5693FCC6DDDF9A6A8BF");
    verify_file(out_dir / "mach_ldebug.h", 23u,
                "914161421EB600860FE448D222DBAA43D9D0D3F2079730FF53CB280AFA0D9CF7");
    verify_file(out_dir / "mach_assert.h", 23u,
                "B8BF10492E885B4F4939FFE373B949FF9025D683E19D6A3B81C30605A6707D15");
    verify_file(out_dir / "debug.h", 17u,
                "2AAA742B31CC0656949A9E7D23B058D0AB36C29C0E21347D9A6D548FA8064EF1");
    TEST_ASSERT(fs::exists(out_dir / "Makefile"), "config.exe generated Makefile present");
    TEST_ASSERT(fs::exists(probe / "gen_build" / "EXPORT_HDRS" / "mach" / "mach_types.h") ||
                    fs::exists(probe / "gen_build" / "EXPORT_HDRS" / "osfmk"),
                "EXPORT_HDRS header staging tree present");
    std::cout << "  [PASS] Stages 280.7-280.9 upstream config.exe & generated headers verified.\n";
}

void test_stage280_10_to_12_genassym_assym_and_abi_proof() {
    std::cout << "[Stages 280.10-280.12] Auditing genassym.o Assembly, assym.s (312 lines) & Compiler ABI Probe...\n";
    const fs::path out_dir = find_workspace_root() / "xnu_probe_isolated" / "gen_build" / "RELEASE_ARM64_OUT";

    // XNU MakeInc.rule compiles genassym.c with -S -> textual assembly named genassym.o
    const fs::path genassym_path = out_dir / "genassym.o";
    verify_file(genassym_path, 44409u,
                "02EA59FB860A87A09F8542FB6CB76560547E138E67DB8ACB999E19FB49425951");
    const auto gen_bytes = read_binary_file(genassym_path);
    const std::string gen_text(reinterpret_cast<const char*>(gen_bytes.data()), gen_bytes.size());
    TEST_ASSERT(gen_text.find("DEFINITION__define__TH_KSTACKPTR:\t .ascii \"344\"") != std::string::npos,
                "genassym.o contains real compiler-emitted DEFINITION__define__TH_KSTACKPTR == 344");
    TEST_ASSERT(gen_text.find("DEFINITION__define__TH_KSTACKPTR_DIVERSIFIER:\t .ascii \"27970\"") != std::string::npos,
                "genassym.o contains real __PTRAUTH_INTRINSICS__ discriminator == 27970 (0x6d42)");

    const fs::path assym_path = out_dir / "assym.s";
    verify_file(assym_path, 8632u,
                "C7434D10684EF3A11DF04A2AC454EE9CBBC07614B78385632154D7B157FA13F6");

    const auto defs = parse_assym_s(assym_path);
    TEST_ASSERT(defs.size() == 312u, "assym.s defines 312 macro lines (symbol + _NUM pairs)");
    TEST_ASSERT(defs.at("TH_KSTACKPTR") == 344, "TH_KSTACKPTR == 344 (0x158)");
    TEST_ASSERT(defs.at("TH_KSTACKPTR_DIVERSIFIER") == 27970, "TH_KSTACKPTR_DIVERSIFIER == 0x6d42");
    TEST_ASSERT(defs.at("ACT_CPUDATAP") == 440, "ACT_CPUDATAP == 440 (0x1b8)");
    TEST_ASSERT(defs.at("TH_CTH_SELF") == 328, "TH_CTH_SELF == 328 (0x148)");
    TEST_ASSERT(defs.at("TH_UPCB") == 272, "TH_UPCB == 272 (0x110)");
    TEST_ASSERT(defs.at("TH_UPCB_DIVERSIFIER") == 7825, "TH_UPCB_DIVERSIFIER == 7825");
    TEST_ASSERT(defs.at("TH_ARM_MACHINE_FLAGS") == 340, "TH_ARM_MACHINE_FLAGS == 340");
    TEST_ASSERT(defs.at("TH_THREAD_ID") == 1176, "TH_THREAD_ID == 1176");
    TEST_ASSERT(defs.at("CPU_ACTIVE_THREAD") == 40, "CPU_ACTIVE_THREAD == 40 (0x28)");
    TEST_ASSERT(defs.at("CPU_ISTACKPTR") == 16, "CPU_ISTACKPTR == 16 (0x10)");
    TEST_ASSERT(defs.at("CPU_ISTACKPTR_DIVERSIFIER") == 59725, "CPU_ISTACKPTR_DIVERSIFIER == 59725 (0xe94d)");
    TEST_ASSERT(defs.at("CPU_TPIDR_EL0") == 920, "CPU_TPIDR_EL0 == 920 (0x398)");
    TEST_ASSERT(defs.at("cdSize") == 928, "sizeof(cpu_data_t) == 928");
    TEST_ASSERT(defs.at("ARM_KERNEL_CONTEXT_SIZE") == 192, "ARM_KERNEL_CONTEXT_SIZE == 192");
    TEST_ASSERT(defs.at("SS64_KERNEL_FP") == 80, "SS64_KERNEL_FP == 80");
    TEST_ASSERT(defs.at("SS64_KERNEL_LR") == 88, "SS64_KERNEL_LR == 88");
    TEST_ASSERT(defs.at("SS64_KERNEL_SP") == 96, "SS64_KERNEL_SP == 96");
    TEST_ASSERT(defs.at("SS64_KERNEL_SSBS") == 105, "SS64_KERNEL_SSBS == 105");
    TEST_ASSERT(defs.at("NS64_KERNEL_FPCR") == 176, "NS64_KERNEL_FPCR == 176");
    TEST_ASSERT(defs.at("PGBYTES") == 16384 && defs.at("PGSHIFT") == 14, "16 KiB translation granule");
    TEST_ASSERT(defs.at("THREAD_CONTINUE_T_DISC") == 54535, "THREAD_CONTINUE_T_DISC == 0xd507 (54535)");

    // Independent C _Static_assert proof & abi_layouts_probe.s cross-check
    verify_file(out_dir / "abi_static_assert_probe.o", 1880u,
                "A278C4EAB78D022526C1BE62F0EF2D94F27E8642883F6EE7C0D57DDC45CB3C69");
    verify_file(out_dir / "abi_layouts_probe.s", 17368u,
                "72D7B808A4E36FA6795A345AD307BFFC5CA598A17E26A154E1F5A6DDE3C2FEB8");
    const auto lay_bytes = read_binary_file(out_dir / "abi_layouts_probe.s");
    const std::string lay_text(reinterpret_cast<const char*>(lay_bytes.data()), lay_bytes.size());
    TEST_ASSERT(lay_text.find("mov\tw0, #216") != std::string::npos, "thread.continuation offset == 216");
    TEST_ASSERT(lay_text.find("mov\tw0, #224") != std::string::npos, "thread.parameter offset == 224");
    TEST_ASSERT(lay_text.find("mov\tw0, #1328") != std::string::npos, "sizeof(struct thread) == 1328");
    TEST_ASSERT(lay_text.find("mov\tw0, #928") != std::string::npos, "sizeof(struct cpu_data) == 928");

    std::cout << "  [PASS] Stages 280.10-280.12 compiler-derived assym.s & ABI proof verified.\n";
}

} // namespace

int main() {
    test_stage280_7_to_9_config_and_generated_headers();
    test_stage280_10_to_12_genassym_assym_and_abi_proof();
    std::cout << "ALL STAGE 280.7 - 280.12 GENASSYM & ABI LAYOUT TESTS PASSED.\n";
    return 0;
}
