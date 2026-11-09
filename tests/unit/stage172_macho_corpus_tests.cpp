#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "loader/macho_corpus_builder.h"
#include "loader/real_artifact_harness.h"

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

// Test 1: Complete Corpus Catalog Verification
bool test_corpus_catalog_integrity() {
    std::cout << "[Test 1] Complete Corpus Catalog Verification...\n";

    auto entries = MachOCorpusRegistry::get_all_corpus_entries();
    TEST_ASSERT(entries.size() == 7, "Corpus must contain exactly 7 standardized artifacts");

    for (const auto& e : entries) {
        TEST_ASSERT(!e.name.empty(), "Entry must have a name");
        TEST_ASSERT(!e.description.empty(), "Entry must have a description");
        TEST_ASSERT(!e.provenance.empty(), "Entry must have documented provenance");
        TEST_ASSERT(!e.license.empty(), "Entry must have documented license");
        TEST_ASSERT(e.cputype == 0x0100000Cu, "CPU type must be ARM64 (0x0100000C)");
        TEST_ASSERT(e.filetype == 2, "Filetype must be MH_EXECUTE (2)");
        TEST_ASSERT(e.file_size > 0 && e.file_size == e.binary_data.size(), "Size matches binary data");
        TEST_ASSERT(e.sha256.size() == 64, "SHA-256 must be exactly 64 hex characters");
    }

    return true;
}

// Test 2: Hello World Corpus Artifact Admission
bool test_hello_world_admission() {
    std::cout << "[Test 2] Hello World Corpus Artifact Admission...\n";

    auto hw = MachOCorpusRegistry::create_hello_world();
    auto meta = RealArtifactHarness::inspect_buffer(hw.binary_data.data(), hw.binary_data.size(), hw.name);

    TEST_ASSERT(meta.is_macho, "Must be valid Mach-O");
    TEST_ASSERT(meta.is_arm64, "Must be ARM64");
    TEST_ASSERT(!meta.is_encrypted, "Must not be encrypted");
    TEST_ASSERT(meta.decision == hw.expected_decision, "Decision matches expected AdmissionDecision::Admitted");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Can execute in userspace");
    TEST_ASSERT(meta.sha256 == hw.sha256, "Metadata SHA-256 matches entry SHA-256");

    return true;
}

// Test 3: Math Compute & Algorithmic Artifact Admission
bool test_math_compute_admission() {
    std::cout << "[Test 3] Math Compute Artifact Admission...\n";

    auto math = MachOCorpusRegistry::create_math_compute();
    auto meta = RealArtifactHarness::inspect_buffer(math.binary_data.data(), math.binary_data.size(), math.name);

    TEST_ASSERT(meta.is_macho && meta.is_arm64, "Mach-O ARM64 confirmed");
    TEST_ASSERT(meta.decision == AdmissionDecision::Admitted, "Admitted for execution");
    TEST_ASSERT(meta.can_execute_in_userspace(), "Can execute in userspace");
    TEST_ASSERT(math.required_syscalls.size() == 1 && math.required_syscalls[0] == "SYS_exit (1)",
                "Requires only SYS_exit");

    return true;
}

// Test 4: File I/O and Memory Allocator Artifact Admission
bool test_file_io_and_mmap_admission() {
    std::cout << "[Test 4] File I/O and Memory Allocator Artifact Admission...\n";

    auto fio = MachOCorpusRegistry::create_file_io();
    auto meta_fio = RealArtifactHarness::inspect_buffer(fio.binary_data.data(), fio.binary_data.size(), fio.name);
    TEST_ASSERT(meta_fio.is_admitted(), "File I/O binary admitted");

    auto mmap_entry = MachOCorpusRegistry::create_memory_mmap();
    auto meta_mmap = RealArtifactHarness::inspect_buffer(mmap_entry.binary_data.data(), mmap_entry.binary_data.size(), mmap_entry.name);
    TEST_ASSERT(meta_mmap.is_admitted(), "Memory mmap binary admitted");

    return true;
}

// Test 5: Mach IPC Client Artifact Admission
bool test_mach_ipc_client_admission() {
    std::cout << "[Test 5] Mach IPC Client Artifact Admission...\n";

    auto ipc = MachOCorpusRegistry::create_mach_ipc_client();
    auto meta_ipc = RealArtifactHarness::inspect_buffer(ipc.binary_data.data(), ipc.binary_data.size(), ipc.name);
    TEST_ASSERT(meta_ipc.is_admitted(), "Mach IPC binary admitted");
    TEST_ASSERT(meta_ipc.can_execute_in_userspace(), "Executable in userspace");

    return true;
}

// Test 6: Chained Fixup Dynamic Binary Admission
bool test_chained_fixup_admission() {
    std::cout << "[Test 6] Chained Fixup Dynamic Binary Admission...\n";

    auto cf = MachOCorpusRegistry::create_dynamic_chained_fixup();
    auto meta_cf = RealArtifactHarness::inspect_buffer(cf.binary_data.data(), cf.binary_data.size(), cf.name);
    TEST_ASSERT(meta_cf.is_admitted(), "Dynamic binary with chained fixups admitted");
    TEST_ASSERT(meta_cf.can_execute_in_userspace(), "Executable in userspace");

    return true;
}

// Test 7: Proprietary FairPlay Encrypted Binary Strict Isolation
bool test_fairplay_encrypted_isolation() {
    std::cout << "[Test 7] Proprietary FairPlay Encrypted Binary Isolation...\n";

    auto enc = MachOCorpusRegistry::create_fairplay_encrypted_app();
    auto meta_enc = RealArtifactHarness::inspect_buffer(enc.binary_data.data(), enc.binary_data.size(), enc.name);

    TEST_ASSERT(meta_enc.is_encrypted, "Detected as encrypted");
    TEST_ASSERT(meta_enc.cryptid == 1, "cryptid == 1");
    TEST_ASSERT(meta_enc.decision == AdmissionDecision::Rejected_Encrypted, "Admission decision is Rejected_Encrypted");
    TEST_ASSERT(!meta_enc.is_admitted(), "Encrypted binary strictly NOT admitted");
    TEST_ASSERT(!meta_enc.can_execute_in_userspace(), "Cannot execute in userspace");
    TEST_ASSERT(!RealArtifactHarness::can_execute_without_sep_keys(meta_enc), "Blocked at SEP hardware key boundary");

    return true;
}

} // namespace

int main() {
    std::cout << "=========================================================\n";
    std::cout << "STAGE 172: Genuine Mach-O Corpus & Provenance Verification\n";
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

    run_test(test_corpus_catalog_integrity, "Complete Corpus Catalog Integrity");
    run_test(test_hello_world_admission, "Hello World Corpus Artifact Admission");
    run_test(test_math_compute_admission, "Math Compute & Algorithmic Artifact Admission");
    run_test(test_file_io_and_mmap_admission, "File I/O & Memory Allocator Admission");
    run_test(test_mach_ipc_client_admission, "Mach IPC Client Artifact Admission");
    run_test(test_chained_fixup_admission, "Chained Fixup Dynamic Binary Admission");
    run_test(test_fairplay_encrypted_isolation, "FairPlay Encrypted Binary Isolation");

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
    return (failed == 0) ? 0 : 1;
}
