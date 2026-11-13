// =============================================================================
// stage_v3_large_opensource_tests.cpp
//
// Campaign 191-V Stage V3:
//   Executes 3 unmodified upstream open-source C libraries via guest minidyld
//   + libSystem.B.dylib under 16 KB Stage-1 Normal-Memory MMU:
//     1. B-Con/crypto-algorithms SHA-256 (O0, O2, aarch64-ios chained fixups)
//     2. DaveGamble/cJSON v1.7.18 (O0, O2, aarch64-ios chained fixups)
//     3. pfalcon/uzlib v2.9.5 LZ77+Huffman deflate/inflate (O0, O2, aarch64-ios)
//   Compares guest stdout & exit codes strictly against independent Windows x64
//   host compilation ground truth.
// =============================================================================

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "cpu/cpu.h"
#include "loader/real_artifact_harness.h"
#include "memory/memory.h"
#include "runtime/darwin_bsd_subsystem.h"
#include "runtime/darwin_mach_ipc.h"
#include "runtime/darwin_userspace_engine.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

namespace {

int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ \
                      << ": " << #cond << "\n"; \
            ++g_failed; \
        } \
    } while (0)

std::string resolve_path(const std::string& rel) {
    const std::vector<std::string> candidates = {
        rel,
        "WinIOS/" + rel,
        "../" + rel,
        "../../" + rel,
        "D:/WinISO26/WinIOS/" + rel
    };
    for (const auto& c : candidates) {
        std::ifstream f(c, std::ios::binary);
        if (f.good()) return c;
    }
    return rel;
}

std::vector<std::uint8_t> load_bin(const std::string& rel) {
    const std::string p = resolve_path(rel);
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return {};
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buf(sz);
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(sz));
    return buf;
}

void run_v3_binary(const char* name, const std::string& rel_path,
                   const std::vector<std::string>& expected_substrings,
                   bool expect_ios) {
    auto dyld_bytes = load_bin("tests/artifacts/bin/minidyld");
    auto libsys_bytes = load_bin("tests/artifacts/bin/libSystem.B.dylib");
    auto app_bytes = load_bin(rel_path);
    CHECK(!dyld_bytes.empty());
    CHECK(!libsys_bytes.empty());
    CHECK(!app_bytes.empty());

    auto meta = RealArtifactHarness::inspect_buffer(app_bytes.data(), app_bytes.size(), rel_path);
    CHECK(meta.is_macho && meta.is_arm64 && meta.can_execute_in_userspace());
    if (expect_ios) {
        bool found_chained = false;
        const auto* b = app_bytes.data();
        if (app_bytes.size() >= 32) {
            std::uint32_t ncmds = 0;
            std::memcpy(&ncmds, b + 16, 4);
            std::size_t off = 32;
            for (std::uint32_t i = 0; i < ncmds && off + 8 <= app_bytes.size(); ++i) {
                std::uint32_t cmd = 0, cmdsize = 0;
                std::memcpy(&cmd, b + off, 4);
                std::memcpy(&cmdsize, b + off + 4, 4);
                if (cmd == kCmdDyldChainedFixups) {
                    found_chained = true;
                }
                if (cmdsize == 0) break;
                off += cmdsize;
            }
        }
        CHECK(found_chained);
    }

    CPU cpu;
    Memory memory(64 * 1024 * 1024);
    DarwinBsdSubsystem bsd;
    MachIpcSubsystem ipc;
    DarwinUserspaceEngine engine(cpu, memory, bsd, ipc);
    bsd.vfs().write_file("/usr/lib/libSystem.B.dylib", libsys_bytes.data(), libsys_bytes.size());

    DarwinPipelineConfig cfg;
    cfg.executable_path = std::string("/usr/bin/") + name;
    cfg.argv = {cfg.executable_path};
    cfg.max_instructions = 5000000;

    auto res = engine.execute_macho_with_guest_dyld(
        dyld_bytes.data(), dyld_bytes.size(),
        app_bytes.data(), app_bytes.size(),
        cfg, 800);

    std::cout << "  [" << name << "] bytes=" << app_bytes.size()
              << " sha256=" << RealArtifactHarness::compute_sha256(app_bytes.data(), app_bytes.size()).substr(0, 12)
              << "... retired=" << res.guest_instructions_retired
              << " exit=" << res.exit_code
              << " err=\"" << res.error_message << "\"\n";
    CHECK(res.is_clean_exit());
    for (const auto& sub : expected_substrings) {
        if (res.stdout_output.find(sub) == std::string::npos) {
            std::cerr << "[FAIL] " << name << " missing \"" << sub << "\", got:\n"
                      << res.stdout_output << "\n";
            ++g_failed;
        }
    }
}

void test_sha256_variants() {
    std::cout << "[Suite 1] Upstream B-Con/crypto-algorithms SHA-256 (O0, O2, aarch64-ios)...\n";
    const std::vector<std::string> expected = {
        "SHA256_EMPTY=e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n",
        "SHA256_ABC=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n",
        "SHA256_TWO=248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1\n",
        "SHA256_CHAIN=0706a918a739779905dded2b5451fd6b5581448bf7948ae06c4c4167e9a7f00f\n",
    };
    run_v3_binary("sha256_app_O0", "tests/artifacts/bin/sha256_app_O0", expected, false);
    run_v3_binary("sha256_app_O2", "tests/artifacts/bin/sha256_app_O2", expected, false);
    run_v3_binary("sha256_app_ios", "tests/artifacts/bin/sha256_app_ios", expected, true);
}

void test_cjson_variants() {
    std::cout << "[Suite 2] Upstream DaveGamble/cJSON v1.7.18 (O0, O2, aarch64-ios)...\n";
    const std::vector<std::string> expected = {
        "CJSON_CMP=1 PRINTED={\"dev\":\"iPhone15,4\",\"gran\":16388,\"pi_x2\":6.28318,\"tag\":\"v3\",\"nums\":[3,14,25,36,47]} HASH=0x684ac634d54c4e10\n",
    };
    run_v3_binary("cjson_app_O0", "tests/artifacts/bin/cjson_app_O0", expected, false);
    run_v3_binary("cjson_app_O2", "tests/artifacts/bin/cjson_app_O2", expected, false);
    run_v3_binary("cjson_app_ios", "tests/artifacts/bin/cjson_app_ios", expected, true);
}

void test_uzlib_variants() {
    std::cout << "[Suite 3] Upstream pfalcon/uzlib v2.9.5 Deflate/Inflate (O0, O2, aarch64-ios)...\n";
    const std::vector<std::string> expected = {
        "UZLIB_RC=1 EQ=1 IN=1536 COMP=833 OUT=1536 CRC=aa542b1d ADLER=16aacbb8 HASH=0xf85a6b32a579dda3\n",
    };
    run_v3_binary("uzlib_app_O0", "tests/artifacts/bin/uzlib_app_O0", expected, false);
    run_v3_binary("uzlib_app_O2", "tests/artifacts/bin/uzlib_app_O2", expected, false);
    run_v3_binary("uzlib_app_ios", "tests/artifacts/bin/uzlib_app_ios", expected, true);
}

} // namespace

int main() {
    test_sha256_variants();
    test_cjson_variants();
    test_uzlib_variants();

    if (g_failed != 0) {
        std::cerr << "stage_v3_large_opensource_tests: " << g_failed << " check(s) failed\n";
        return 1;
    }
    std::cout << "ALL STAGE V3 LARGE OPEN-SOURCE PROGRAM TESTS PASSED (9/9 VARIANTS MATCH HOST TRUTH)\n";
    return 0;
}
