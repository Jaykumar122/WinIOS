#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/apple_artifact_admission.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

inline std::uint32_t static_cmd_cast(std::size_t s) {
    return static_cast<std::uint32_t>(s);
}

// Helper to construct an encrypted IM4P container simulation
std::vector<std::uint8_t> make_encrypted_im4p() {
    std::vector<std::uint8_t> buf(512, 0);
    buf[0] = 'I'; buf[1] = 'M'; buf[2] = '4'; buf[3] = 'P';
    buf[4] = 'k'; buf[5] = 'r'; buf[6] = 'n'; buf[7] = 'l';
    const char desc[] = "Encrypted Kernel Collection (KBAG: AES-256-CBC, SEP GID key required)";
    std::memcpy(buf.data() + 8, desc, sizeof(desc));
    return buf;
}

// Helper to inject an LC_ENCRYPTION_INFO_64 load command into a Mach-O binary
std::vector<std::uint8_t> make_encrypted_macho(std::uint32_t cryptid = 1) {
    auto base_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    const auto* old_hdr = reinterpret_cast<const MachOHeader64Raw*>(base_bin.data());
    const std::size_t insert_pos = sizeof(MachOHeader64Raw) + old_hdr->sizeofcmds;

    struct {
        std::uint32_t cmd{0x0000002Cu}; // LC_ENCRYPTION_INFO_64
        std::uint32_t cmdsize{24};
        std::uint32_t cryptoff{0x4000};
        std::uint32_t cryptsize{0x4000};
        std::uint32_t cryptid{1};
        std::uint32_t pad{0};
    } enc_cmd;
    enc_cmd.cryptid = cryptid;

    std::vector<std::uint8_t> result = base_bin;
    const auto* enc_bytes = reinterpret_cast<const std::uint8_t*>(&enc_cmd);
    result.insert(result.begin() + insert_pos, enc_bytes, enc_bytes + sizeof(enc_cmd));

    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(result.data());
    hdr->ncmds += 1;
    hdr->sizeofcmds += static_cmd_cast(sizeof(enc_cmd));

    return result;
}

// Helper to construct foreign architecture Mach-O (e.g. x86_64)
std::vector<std::uint8_t> make_x86_64_macho() {
    std::vector<std::uint8_t> buf(sizeof(MachOHeader64Raw), 0);
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(buf.data());
    hdr->magic = 0xFEEDFACFu; // MH_MAGIC_64
    hdr->cputype = 0x01000007u; // CPU_TYPE_X86_64
    hdr->cpusubtype = 3;
    hdr->filetype = 2; // MH_EXECUTE
    return buf;
}

// Test 1: Synthetic ARM64 & ARM64e Mach-O Executables Admission
bool test_synthetic_macho_admission() {
    std::cout << "[Test 1] Synthetic ARM64 & ARM64e Mach-O Executables Admission...\n";

    auto arm64_bin = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    auto arm64e_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();

    auto meta_arm64 = RealArtifactHarness::inspect_buffer(arm64_bin.data(), arm64_bin.size(), "/usr/bin/arm64_tool");
    TEST_ASSERT(meta_arm64.is_macho, "ARM64 fixture is recognized as Mach-O");
    TEST_ASSERT(meta_arm64.is_arm64, "ARM64 architecture detected");
    TEST_ASSERT(!meta_arm64.is_encrypted, "Synthetic binary is not encrypted");
    TEST_ASSERT(meta_arm64.is_admitted(), "Synthetic ARM64 binary is admitted");
    TEST_ASSERT(meta_arm64.can_execute_in_userspace(), "Can execute in userspace");
    TEST_ASSERT(RealArtifactHarness::can_execute_without_sep_keys(meta_arm64), "Can execute without SEP keys");

    auto meta_arm64e = RealArtifactHarness::inspect_buffer(arm64e_bin.data(), arm64e_bin.size(), "/usr/bin/arm64e_tool");
    TEST_ASSERT(meta_arm64e.is_macho, "ARM64e fixture is recognized as Mach-O");
    TEST_ASSERT(meta_arm64e.is_arm64, "ARM64 general family detected");
    TEST_ASSERT(meta_arm64e.is_arm64e, "ARM64e specific subtype detected");
    TEST_ASSERT(!meta_arm64e.is_encrypted, "Synthetic binary is not encrypted");
    TEST_ASSERT(meta_arm64e.is_admitted(), "Synthetic ARM64e binary is admitted");
    TEST_ASSERT(meta_arm64e.can_execute_in_userspace(), "Can execute in userspace");

    return true;
}

// Test 2: Open-Source Darwin Utility Simulation (Unencrypted Binary Corpus)
bool test_unencrypted_darwin_corpus() {
    std::cout << "[Test 2] Open-Source Darwin Utility Simulation...\n";

    // Create binary with custom instructions (mov x0, #42; ret)
    // mov x0, #42 => 0xd2800540, ret => 0xd65f03c0 (little endian)
    std::vector<std::uint8_t> code = {
        0x40, 0x05, 0x80, 0xd2, // mov x0, #42
        0xc0, 0x03, 0x5f, 0xd6  // ret
    };

    auto util_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(
        "/usr/lib/dyld",
        {"/usr/lib/libSystem.B.dylib"},
        code
    );

    auto meta = RealArtifactHarness::inspect_buffer(util_bin.data(), util_bin.size(), "/bin/echo");
    TEST_ASSERT(meta.is_macho, "Utility recognized as Mach-O");
    TEST_ASSERT(meta.is_arm64e, "ARM64e confirmed");
    TEST_ASSERT(!meta.is_encrypted, "Unencrypted open-source utility");
    TEST_ASSERT(meta.decision == AdmissionDecision::Admitted, "Admitted by gate");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Can execute in userspace");
    TEST_ASSERT(!meta.requires_sep_gid, "Does not require SEP GID");

    return true;
}

// Test 3: Authentic FairPlay / App Store Encrypted Binary Rejection
bool test_fairplay_encrypted_rejection() {
    std::cout << "[Test 3] FairPlay / App Store Encrypted Binary Rejection...\n";

    auto enc_bin = make_encrypted_macho(1); // cryptid = 1
    auto meta = RealArtifactHarness::inspect_buffer(enc_bin.data(), enc_bin.size(), "/Applications/App.app/App");

    TEST_ASSERT(meta.is_macho, "Mach-O container recognized");
    TEST_ASSERT(meta.is_encrypted, "Encrypted flag set");
    TEST_ASSERT(meta.cryptid == 1, "cryptid == 1 identified");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_Encrypted, "Admission decision is Rejected_Encrypted");
    TEST_ASSERT(!meta.is_admitted(), "Encrypted binary is NOT admitted");
    TEST_ASSERT(!meta.can_execute_in_userspace(), "Encrypted binary cannot execute in userspace");
    TEST_ASSERT(!RealArtifactHarness::can_execute_without_sep_keys(meta), "Blocked without SEP/FairPlay keys");

    return true;
}

// Test 4: IMG4 / IM4P Protected Kernel Collection & SecureBoot Container Rejection
bool test_img4_protected_container_rejection() {
    std::cout << "[Test 4] IMG4 / IM4P Protected Kernel Collection Rejection...\n";

    auto im4p_buf = make_encrypted_im4p();
    auto meta = RealArtifactHarness::inspect_buffer(im4p_buf.data(), im4p_buf.size(), "/System/Library/Caches/com.apple.kernelcaches/kernelcache");

    TEST_ASSERT(meta.is_img4_container, "Recognized as IMG4/IM4P container");
    TEST_ASSERT(meta.requires_sep_gid, "Flagged as requiring SEP GID key");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_Encrypted, "Decision is Rejected_Encrypted");
    TEST_ASSERT(!meta.is_admitted(), "Container is NOT admitted for userspace");
    TEST_ASSERT(!meta.can_execute_in_userspace(), "Cannot execute directly in userspace");

    return true;
}

// Test 5: Foreign Architecture & Corrupt Binary Corpus Rejection
bool test_foreign_and_corrupt_rejection() {
    std::cout << "[Test 5] Foreign Architecture & Corrupt Binary Corpus Rejection...\n";

    // 1. x86_64 Mach-O
    auto x86_bin = make_x86_64_macho();
    auto meta_x86 = RealArtifactHarness::inspect_buffer(x86_bin.data(), x86_bin.size(), "/bin/x86_bin");
    TEST_ASSERT(meta_x86.is_macho, "Mach-O header found");
    TEST_ASSERT(!meta_x86.is_arm64, "Not ARM64");
    TEST_ASSERT(meta_x86.decision == AdmissionDecision::Rejected_ArchitectureMismatch, "Rejected for architecture mismatch");
    TEST_ASSERT(!meta_x86.is_admitted(), "x86_64 is not admitted on A16 platform");

    // 2. Truncated buffer (< 32 bytes)
    std::vector<std::uint8_t> short_buf = { 0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00 };
    auto meta_short = RealArtifactHarness::inspect_buffer(short_buf.data(), short_buf.size(), "/bin/truncated");
    TEST_ASSERT(meta_short.decision == AdmissionDecision::Rejected_Truncated, "Rejected truncated header");

    // 3. Corrupt Magic
    std::vector<std::uint8_t> corrupt_buf(64, 0xAA);
    auto meta_corrupt = RealArtifactHarness::inspect_buffer(corrupt_buf.data(), corrupt_buf.size(), "/bin/corrupt");
    TEST_ASSERT(meta_corrupt.decision == AdmissionDecision::Rejected_MalformedHeader, "Rejected malformed header");

    return true;
}

// Test 6: Cryptographic Digesting & Corpus Fingerprinting Determinism
bool test_sha256_cryptographic_fingerprinting() {
    std::cout << "[Test 6] Cryptographic Digesting & Corpus Fingerprinting Determinism...\n";

    // 1. Determinism across runs on ARM64 binary
    auto bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    std::string hash1 = RealArtifactHarness::compute_sha256(bin.data(), bin.size());
    std::string hash2 = RealArtifactHarness::compute_sha256(bin.data(), bin.size());
    TEST_ASSERT(!hash1.empty(), "SHA-256 digest is non-empty");
    TEST_ASSERT(hash1 == hash2, "SHA-256 calculation is deterministic");
    TEST_ASSERT(hash1.size() == 64, "SHA-256 output is 64 hex characters");

    // 2. Avalanche effect: changing one byte modifies hash completely
    bin[100] ^= 0x01;
    std::string hash_mod = RealArtifactHarness::compute_sha256(bin.data(), bin.size());
    TEST_ASSERT(hash1 != hash_mod, "Single-bit flip completely changes SHA-256 digest");

    return true;
}

// Test 7: Admission Gate Zero-Leakage Policy Enforcement
bool test_zero_leakage_policy_enforcement() {
    std::cout << "[Test 7] Admission Gate Zero-Leakage Policy Enforcement...\n";

    // Test matrix across artifact variants
    struct TestCase {
        std::string name;
        std::vector<std::uint8_t> data;
        bool expected_admission;
        bool expected_executable;
    };

    std::vector<TestCase> cases = {
        {"Admitted_Synthetic_ARM64", UserspaceMachOFixtureBuilder::create_valid_arm64_executable(), true, true},
        {"Admitted_Synthetic_ARM64e", UserspaceMachOFixtureBuilder::create_valid_arm64e_executable(), true, true},
        {"Blocked_FairPlay_Encrypted", make_encrypted_macho(1), false, false},
        {"Blocked_KBAG_IM4P", make_encrypted_im4p(), false, false},
        {"Rejected_Foreign_x86_64", make_x86_64_macho(), false, false}
    };

    for (const auto& tc : cases) {
        auto meta = RealArtifactHarness::inspect_buffer(tc.data.data(), tc.data.size(), tc.name);
        TEST_ASSERT(meta.is_admitted() == tc.expected_admission,
                    ("Admission check failed for: " + tc.name).c_str());
        TEST_ASSERT(meta.can_execute_in_userspace() == tc.expected_executable,
                    ("Executable check failed for: " + tc.name).c_str());
    }

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 167: Genuine ARM64 Mach-O Test Corpus\n";
    std::cout << "=========================================================\n";

    int passed = 0;
    int failed = 0;

    auto run_test = [&](auto test_fn, const char* name) {
        if (test_fn()) {
            std::cout << "[PASS] " << name << "\n";
            passed++;
        } else {
            std::cout << "[FAIL] " << name << "\n";
            failed++;
        }
    };

    run_test(test_synthetic_macho_admission, "Synthetic ARM64/ARM64e Admission");
    run_test(test_unencrypted_darwin_corpus, "Unencrypted Darwin Utility Simulation");
    run_test(test_fairplay_encrypted_rejection, "FairPlay Encrypted Binary Rejection");
    run_test(test_img4_protected_container_rejection, "IMG4/IM4P Protected Container Rejection");
    run_test(test_foreign_and_corrupt_rejection, "Foreign & Corrupted Binary Rejection");
    run_test(test_sha256_cryptographic_fingerprinting, "SHA-256 Digest Determinism & Avalanche");
    run_test(test_zero_leakage_policy_enforcement, "Zero-Leakage Policy Enforcement");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
