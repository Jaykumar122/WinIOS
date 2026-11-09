#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"

using namespace winios::loader;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

namespace {

// Helper to construct an encrypted IM4P container simulation
std::vector<std::uint8_t> make_encrypted_im4p() {
    std::vector<std::uint8_t> buf(512, 0);
    buf[0] = 'I'; buf[1] = 'M'; buf[2] = '4'; buf[3] = 'P';
    buf[4] = 'k'; buf[5] = 'r'; buf[6] = 'n'; buf[7] = 'l';
    const char desc[] = "Encrypted Kernel Collection (KBAG: AES-256-CBC, SEP GID key required)";
    std::memcpy(buf.data() + 8, desc, sizeof(desc));
    return buf;
}

// Helper to construct an unencrypted IMG4 container
std::vector<std::uint8_t> make_unencrypted_img4() {
    std::vector<std::uint8_t> buf(512, 0);
    buf[0] = 0x30; buf[1] = 0x82; buf[2] = 0x01; buf[3] = 0xFC;
    buf[4] = 'I'; buf[5] = 'M'; buf[6] = 'G'; buf[7] = '4';
    buf[16] = 'I'; buf[17] = 'M'; buf[18] = '4'; buf[19] = 'M';
    buf[32] = 'I'; buf[33] = 'M'; buf[34] = '4'; buf[35] = 'P';
    const char desc[] = "Decrypted Kernel Collection (Production unencrypted release)";
    std::memcpy(buf.data() + 48, desc, sizeof(desc));
    return buf;
}


// Helper to cast size
inline std::uint32_t static_cmd_cast(std::size_t s) {
    return static_cast<std::uint32_t>(s);
}

// Helper to inject an LC_ENCRYPTION_INFO_64 load command into a Mach-O binary
std::vector<std::uint8_t> make_encrypted_macho(std::uint32_t cryptid = 1) {
    auto base_bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    const auto* old_hdr = reinterpret_cast<const MachOHeader64Raw*>(base_bin.data());
    const std::size_t insert_pos = sizeof(MachOHeader64Raw) + old_hdr->sizeofcmds;

    struct {
        std::uint32_t cmd{0x0000002Cu};
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

    // Update mach_header_64: increment ncmds and add cmdsize to sizeofcmds
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(result.data());
    hdr->ncmds += 1;
    hdr->sizeofcmds += static_cmd_cast(sizeof(enc_cmd));

    return result;
}


// Test 1: Real Unencrypted ARM64 Mach-O Admission
bool test_unencrypted_arm64_macho_admitted() {
    std::cout << "[Test 1] Real Unencrypted ARM64 Mach-O Admission...\n";

    auto bin = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    auto meta = RealArtifactHarness::inspect_buffer(bin.data(), bin.size(), "/usr/bin/tar");

    TEST_ASSERT(meta.is_macho, "Must be identified as Mach-O");
    TEST_ASSERT(meta.is_arm64, "Must be identified as ARM64");
    TEST_ASSERT(!meta.is_arm64e, "Must not be ARM64e");
    TEST_ASSERT(!meta.is_encrypted, "Must not be encrypted");
    TEST_ASSERT(!meta.requires_sep_gid, "Must not require SEP GID key");
    TEST_ASSERT(meta.is_admitted(), "Unencrypted ARM64 binary must be admitted");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Must be permitted to execute in userspace");
    TEST_ASSERT(RealArtifactHarness::can_execute_without_sep_keys(meta), "Must execute without SEP hardware");

    return true;
}

// Test 2: Real Unencrypted ARM64e Mach-O Admission
bool test_unencrypted_arm64e_macho_admitted() {
    std::cout << "[Test 2] Real Unencrypted ARM64e Mach-O Admission...\n";

    auto bin = UserspaceMachOFixtureBuilder::create_valid_arm64e_executable();
    auto meta = RealArtifactHarness::inspect_buffer(bin.data(), bin.size(), "/sbin/launchd");

    TEST_ASSERT(meta.is_macho, "Must be identified as Mach-O");
    TEST_ASSERT(meta.is_arm64, "Must be ARM64");
    TEST_ASSERT(meta.is_arm64e, "Must be identified as ARM64e");
    TEST_ASSERT(!meta.is_encrypted, "Must not be encrypted");
    TEST_ASSERT(meta.is_admitted(), "Unencrypted ARM64e binary must be admitted");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Must be permitted to execute in userspace");

    return true;
}

// Test 3: FairPlay / DRM Encrypted Mach-O Rejection (cryptid != 0)
bool test_fairplay_encrypted_macho_rejected() {
    std::cout << "[Test 3] FairPlay / DRM Encrypted Mach-O Rejection (cryptid != 0)...\n";

    auto enc_bin = make_encrypted_macho(1); // cryptid = 1
    auto meta = RealArtifactHarness::inspect_buffer(enc_bin.data(), enc_bin.size(), "/Applications/App.app/App");

    TEST_ASSERT(meta.is_macho, "Must be Mach-O");
    TEST_ASSERT(meta.is_encrypted, "Must detect encrypted load command");
    TEST_ASSERT(meta.cryptid == 1, "cryptid must be 1");
    TEST_ASSERT(!meta.is_admitted(), "Encrypted binary must not be admitted");
    TEST_ASSERT(!meta.can_execute_in_userspace(), "Encrypted binary cannot execute in userspace");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_Encrypted, "Decision must be Rejected_Encrypted");
    // SEP Decoupling: FairPlay DRM is application-level and does not require SEP hardware keys
    TEST_ASSERT(!meta.requires_sep_gid, "FairPlay DRM does not require SEP GID key");

    return true;
}

// Test 4: SEP GID Encrypted IM4P Firmware Blocked (Hardware Key Boundary)
bool test_sep_gid_encrypted_im4p_blocked() {
    std::cout << "[Test 4] SEP GID Encrypted IM4P Firmware Blocked (Hardware Key Boundary)...\n";

    auto im4p = make_encrypted_im4p();
    auto meta = RealArtifactHarness::inspect_buffer(im4p.data(), im4p.size(), "kernelcache.release.iphone15");

    TEST_ASSERT(meta.is_img4_container, "Must be identified as IM4P/IMG4 container");
    TEST_ASSERT(meta.is_encrypted, "Must be identified as encrypted");
    TEST_ASSERT(meta.requires_sep_gid, "Must require Apple SEP GID hardware keys");
    TEST_ASSERT(!meta.is_admitted(), "Encrypted IM4P must be rejected");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_Encrypted, "Decision must be Rejected_Encrypted");
    TEST_ASSERT(!meta.can_execute_in_userspace(), "Cannot execute without SEP hardware keys (BLOCKED)");

    return true;
}

// Test 5: Unencrypted IMG4 Container Admitted
bool test_unencrypted_img4_container_admitted() {
    std::cout << "[Test 5] Unencrypted IMG4 Container Admitted...\n";

    auto img4 = make_unencrypted_img4();
    auto meta = RealArtifactHarness::inspect_buffer(img4.data(), img4.size(), "kernelcache.decrypted");

    TEST_ASSERT(meta.is_img4_container, "Must be identified as IMG4 container");
    TEST_ASSERT(!meta.is_encrypted, "Must be unencrypted");
    TEST_ASSERT(!meta.requires_sep_gid, "Does not require SEP GID keys");
    TEST_ASSERT(meta.is_admitted(), "Decrypted IMG4 must be admitted");

    return true;
}

// Test 6: Architecture Mismatch Rejection (x86_64)
bool test_architecture_mismatch_rejected() {
    std::cout << "[Test 6] Architecture Mismatch Rejection (x86_64)...\n";

    auto bin = UserspaceMachOFixtureBuilder::create_valid_arm64_executable();
    // Overwrite cputype with CPU_TYPE_X86_64 (0x01000007)
    auto* hdr = reinterpret_cast<MachOHeader64Raw*>(bin.data());
    hdr->cputype = 0x01000007u;

    auto meta = RealArtifactHarness::inspect_buffer(bin.data(), bin.size(), "/usr/bin/x86_tool");

    TEST_ASSERT(meta.is_macho, "Must be Mach-O");
    TEST_ASSERT(!meta.is_arm64, "Must not be ARM64");
    TEST_ASSERT(!meta.is_admitted(), "x86_64 binary must be rejected");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_ArchitectureMismatch,
                "Decision must be Rejected_ArchitectureMismatch");

    return true;
}

// Test 7: SHA-256 Calculation & Truncated Header Handling
bool test_sha256_calculation_and_truncation() {
    std::cout << "[Test 7] SHA-256 Calculation & Truncated Header Handling...\n";

    // 1. Valid SHA-256 calculation
    const std::uint8_t test_bytes[] = "WinIOS Apple Artifact Test Vector";
    std::string hash = RealArtifactHarness::compute_sha256(test_bytes, sizeof(test_bytes) - 1);
    TEST_ASSERT(!hash.empty(), "SHA-256 hash must not be empty");
    TEST_ASSERT(hash.length() == 64, "SHA-256 hex string must be exactly 64 characters");

    // 2. Truncated header
    std::uint8_t tiny[16] = {0};
    auto meta = RealArtifactHarness::inspect_buffer(tiny, sizeof(tiny), "truncated.bin");
    TEST_ASSERT(!meta.is_admitted(), "Truncated file must be rejected");
    TEST_ASSERT(meta.decision == AdmissionDecision::Rejected_Truncated, "Must be Rejected_Truncated");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 164: Genuine Artifact Admission & Execution Harness\n";
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

    run_test(test_unencrypted_arm64_macho_admitted, "Real Unencrypted ARM64 Mach-O Admission");
    run_test(test_unencrypted_arm64e_macho_admitted, "Real Unencrypted ARM64e Mach-O Admission");
    run_test(test_fairplay_encrypted_macho_rejected, "FairPlay Encrypted Mach-O Rejection");
    run_test(test_sep_gid_encrypted_im4p_blocked, "SEP GID Encrypted IM4P Firmware Blocked");
    run_test(test_unencrypted_img4_container_admitted, "Unencrypted IMG4 Container Admitted");
    run_test(test_architecture_mismatch_rejected, "Architecture Mismatch Rejection");
    run_test(test_sha256_calculation_and_truncation, "SHA-256 Calculation & Truncation Handling");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
