#include "loader/app_inspector.h"
#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "packaging/app_package.h"
#include "packaging/canonical_path_validator.h"
#include "security/developer_keystore.h"
#include "security/publisher_trust_store.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <crtdbg.h>
#endif

using namespace winios;
using namespace winios::loader;
using namespace winios::packaging;
using namespace winios::security;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at line " << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

// =============================================================================
// Reproducible Fuzzing Harness Architecture
// =============================================================================

enum class MutationStrategy : uint32_t {
    BitFlip = 0,
    ByteSwap,
    ArithmeticDelta,
    BoundaryValue,
    BlockDeletion,
    BlockInsertion,
    RandomOverwrite,
    Count
};

struct FuzzCrashArtifact {
    uint64_t                  seed{0};
    uint64_t                  iteration{0};
    std::string               target_subsystem;
    MutationStrategy          mutation_used{MutationStrategy::BitFlip};
    std::vector<uint8_t>      mutated_input;
    std::string               diagnostic_message;

    std::string serialize() const {
        std::ostringstream oss;
        oss << "SEED=" << seed << "\n"
            << "ITER=" << iteration << "\n"
            << "TARGET=" << target_subsystem << "\n"
            << "STRATEGY=" << static_cast<uint32_t>(mutation_used) << "\n"
            << "LEN=" << mutated_input.size() << "\n"
            << "DIAG=" << diagnostic_message << "\n";
        return oss.str();
    }
};

class DeterministicMutator {
private:
    std::mt19937_64 rng_;
    const uint64_t initial_seed_;

public:
    explicit DeterministicMutator(uint64_t seed)
        : rng_(seed), initial_seed_(seed) {}

    uint64_t seed() const noexcept { return initial_seed_; }

    std::vector<uint8_t> mutate(const std::vector<uint8_t>& seed_data, MutationStrategy& out_strat) {
        if (seed_data.empty()) return {0x00};

        std::vector<uint8_t> out = seed_data;
        const auto strat = static_cast<MutationStrategy>(rng_() % static_cast<uint32_t>(MutationStrategy::Count));
        out_strat = strat;

        switch (strat) {
        case MutationStrategy::BitFlip: {
            size_t byte_idx = static_cast<size_t>(rng_() % out.size());
            uint8_t bit_idx = static_cast<uint8_t>(rng_() % 8);
            out[byte_idx] ^= static_cast<uint8_t>(1u << bit_idx);
            break;
        }
        case MutationStrategy::ByteSwap: {
            if (out.size() > 1) {
                size_t idx1 = static_cast<size_t>(rng_() % out.size());
                size_t idx2 = static_cast<size_t>(rng_() % out.size());
                std::swap(out[idx1], out[idx2]);
            }
            break;
        }
        case MutationStrategy::ArithmeticDelta: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            const int8_t deltas[] = {-16, -8, -1, 1, 8, 16};
            int8_t d = deltas[rng_() % 6];
            out[idx] = static_cast<uint8_t>(out[idx] + d);
            break;
        }
        case MutationStrategy::BoundaryValue: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            const uint8_t boundaries[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF};
            out[idx] = boundaries[rng_() % 6];
            break;
        }
        case MutationStrategy::BlockDeletion: {
            if (out.size() > 16) {
                size_t del_len = 1 + static_cast<size_t>(rng_() % 8);
                size_t start = static_cast<size_t>(rng_() % (out.size() - del_len));
                out.erase(out.begin() + static_cast<ptrdiff_t>(start),
                          out.begin() + static_cast<ptrdiff_t>(start + del_len));
            }
            break;
        }
        case MutationStrategy::BlockInsertion: {
            if (out.size() < 1024 * 1024) {
                size_t ins_len = 1 + static_cast<size_t>(rng_() % 8);
                size_t start = static_cast<size_t>(rng_() % (out.size() + 1));
                std::vector<uint8_t> chunk(ins_len);
                for (auto& b : chunk) b = static_cast<uint8_t>(rng_() & 0xFF);
                out.insert(out.begin() + static_cast<ptrdiff_t>(start), chunk.begin(), chunk.end());
            }
            break;
        }
        case MutationStrategy::RandomOverwrite: {
            size_t idx = static_cast<size_t>(rng_() % out.size());
            out[idx] = static_cast<uint8_t>(rng_() & 0xFF);
            break;
        }
        default:
            break;
        }
        return out;
    }
};

// -----------------------------------------------------------------------------
// Test 1: Path Validator Fuzzing (1,000 Iterations)
// -----------------------------------------------------------------------------
static void test_path_validator_fuzzing() {
    std::cout << "[Stage 234.21] Test 1: Canonical Path Validator Fuzzing (1,000 Iterations)...\n";

    constexpr uint64_t kSeed = 0x5041544846555A5AULL; // "PATHFUZZ"
    DeterministicMutator mutator(kSeed);

    const std::vector<std::string> seed_paths = {
        "bin/main",
        "assets/images/icon.png",
        "config/settings.json",
        "Frameworks/libhelper.dylib",
        "nested/deeply/located/resource.dat"
    };

    size_t rejected_count = 0;
    size_t accepted_count = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        const std::string& base = seed_paths[iter % seed_paths.size()];
        std::vector<uint8_t> raw_bytes(base.begin(), base.end());

        MutationStrategy strat{};
        auto fuzzed = mutator.mutate(raw_bytes, strat);

        std::string test_path(fuzzed.begin(), fuzzed.end());
        std::string norm_out;

        // Invariant: Path validator must never crash, throw, or return unhandled states
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(test_path, norm_out);
        if (status == PathValidationStatus::Valid) {
            accepted_count++;
            // If accepted, invariant: norm_out must not contain double slashes or traversal sequences
            TEST_ASSERT(norm_out.find("//") == std::string::npos, "No double slashes permitted in valid normalized path");
            TEST_ASSERT(norm_out.find("..") == std::string::npos, "No traversal permitted in valid normalized path");
            TEST_ASSERT(!norm_out.empty() && norm_out[0] != '/', "Relative paths only");
        } else {
            rejected_count++;
        }
    }

    std::cout << "  -> Path validator completed 1,000 iterations: "
              << accepted_count << " valid, " << rejected_count << " securely rejected. Zero crashes.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Mach-O Parser & Validator Fuzzing (1,000 Iterations)
// -----------------------------------------------------------------------------
static void test_macho_parser_fuzzing() {
    std::cout << "[Stage 234.21] Test 2: Mach-O Parser & AppInspector Fuzzing (1,000 Iterations)...\n";

    constexpr uint64_t kSeed = 0x4D4143484F46555AULL; // "MACHOFUZ"
    DeterministicMutator mutator(kSeed);

    auto fixture = PublicMachOCorpus::create_minimal_executable();
    const auto& base_bin = fixture.binary_data;

    size_t rejected_count = 0;
    size_t accepted_count = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        MutationStrategy strat{};
        auto fuzzed = mutator.mutate(base_bin, strat);

        // Invariant: inspect_binary must handle any random byte sequence safely
        auto report = AppInspector::inspect_binary(fuzzed.data(), fuzzed.size());
        if (report.is_valid) {
            accepted_count++;
            // If valid, W^X and entry point must be strictly clean
            TEST_ASSERT(report.wx_clean, "Valid fuzzed binary must be W^X clean");
            TEST_ASSERT(report.entry_point_offset != 0, "Valid fuzzed binary must have valid entry");
        } else {
            rejected_count++;
            TEST_ASSERT(!report.rejection_reason.empty(), "Rejection reason must be populated");
        }
    }

    std::cout << "  -> Mach-O fuzzer completed 1,000 iterations: "
              << accepted_count << " valid, " << rejected_count << " safely rejected. Zero crashes.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Package Unpack & Signature Verifier Fuzzing (1,000 Iterations)
// -----------------------------------------------------------------------------
static void test_package_verifier_fuzzing() {
    std::cout << "[Stage 234.21] Test 3: Package Parser & Signature Verifier Fuzzing (1,000 Iterations)...\n";

    constexpr uint64_t kSeed = 0x504B475645524946ULL; // "PKGVERIF"
    DeterministicMutator mutator(kSeed);

    // Create a base valid package
    AppPackage pkg;
    pkg.manifest.format_version = 3;
    pkg.manifest.bundle_id = "com.winios.fuzztest";
    pkg.manifest.name = "FuzzApp";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.add_file("bin/main", {0xCF, 0xFA, 0xED, 0xFE, 0x0C, 0x00, 0x00, 0x01});
    pkg.add_file("Info.plist", {'<', 'p', 'l', 'i', 's', 't', '>', '<', '/', 'p', 'l', 'i', 's', 't', '>'});

    PublisherTrustStore trust_store;
    DeveloperIdentity dev_id = DeveloperKeyStore::generate_identity("com.winios.fuzztest", "Fuzz Org", "key-fuzz-01", PublisherRole::Developer);
    PublisherKey dev_pub = DeveloperKeyStore::export_public_key(dev_id);
    TEST_ASSERT(trust_store.register_key(dev_pub), "Register developer key");

    auto base_packed = pkg.pack_v3();
    bool sign_ok = PublisherTrustStore::sign_package_v2(base_packed, dev_id);
    TEST_ASSERT(sign_ok, "Package signed");

    size_t unpack_fail = 0;
    size_t sig_fail = 0;
    size_t passed = 0;

    for (size_t iter = 0; iter < 1000; ++iter) {
        MutationStrategy strat{};
        auto fuzzed = mutator.mutate(base_packed, strat);

        AppPackage test_pkg;
        if (!AppPackage::unpack(fuzzed.data(), fuzzed.size(), test_pkg)) {
            unpack_fail++;
            continue;
        }

        // If unpack succeeded, signature verification must either pass or fail cleanly without exceptions
        auto sig_result = trust_store.verify_package_signature(fuzzed.data(), fuzzed.size(), TrustPolicy::StrictProduction);
        if (sig_result.is_trusted) {
            passed++;
        } else {
            sig_fail++;
        }
    }

    std::cout << "  -> Package fuzzer completed 1,000 iterations: "
              << unpack_fail << " unpack failures, " << sig_fail << " signature rejections, "
              << passed << " accepted. Zero crashes.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Crash Artifact Serialization & Replay Harness Verification
// -----------------------------------------------------------------------------
static void test_replay_harness() {
    std::cout << "[Stage 234.21] Test 4: Crash Artifact Serialization & Deterministic Replay...\n";

    FuzzCrashArtifact artifact{};
    artifact.seed = 0x123456789ABCDEF0ULL;
    artifact.iteration = 42;
    artifact.target_subsystem = "AppInspector";
    artifact.mutation_used = MutationStrategy::ArithmeticDelta;
    artifact.mutated_input = {0x00, 0x01, 0x02, 0x03, 0x04};
    artifact.diagnostic_message = "Synthetic test artifact";

    // 1. Serialize artifact
    std::string serialized = artifact.serialize();
    TEST_ASSERT(!serialized.empty(), "Artifact serialized");
    TEST_ASSERT(serialized.find("TARGET=AppInspector") != std::string::npos, "Target recorded");

    // 2. Replay target
    auto report1 = AppInspector::inspect_binary(artifact.mutated_input.data(), artifact.mutated_input.size());
    auto report2 = AppInspector::inspect_binary(artifact.mutated_input.data(), artifact.mutated_input.size());

    // Invariant: Replay execution must be 100% deterministic
    TEST_ASSERT(report1.is_valid == report2.is_valid, "Replay validity deterministic");
    TEST_ASSERT(report1.rejection_reason == report2.rejection_reason, "Replay reason deterministic");

    std::cout << "  -> Artifact serialization and replay reproducibility verified.\n";
}

int main() {
#if defined(_WIN32)
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.21: Reproducible Fuzzing Infrastructure Test Suite   \n";
    std::cout << "=================================================================\n";

    test_path_validator_fuzzing();
    test_macho_parser_fuzzing();
    test_package_verifier_fuzzing();
    test_replay_harness();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL REPRODUCIBLE FUZZING INFRASTRUCTURE TESTS PASSED!          \n";
    std::cout << "=================================================================\n";
    return 0;
}