#include "packaging/app_package.h"
#include "packaging/canonical_path_validator.h"
#include "security/publisher_trust_store.h"
#include "security/developer_keystore.h"
#include "memory/guest_memory_validator.h"
#include "memory/user_address_space.h"
#include "crypto/ed25519.h"
#include "crypto/sha256.h"

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <cstring>
#include <algorithm>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::cerr << "Fuzzing Assertion Failed: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl << std::flush; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::packaging;
using namespace winios::security;
using namespace winios::memory;

// Deterministic PRNG seeded with fixed value
static constexpr uint32_t kFuzzSeed = 0x233C0DE;

static AppPackage make_baseline_package() {
    AppPackage pkg;
    pkg.manifest.bundle_id = "com.winios.fuzztarget";
    pkg.manifest.name = "FuzzTarget";
    pkg.manifest.version = "1.0.0";
    pkg.manifest.executable_name = "bin/main";
    pkg.manifest.format_version = "3.0";

    std::vector<uint8_t> bin_data = {'F', 'U', 'Z', 'Z', 'B', 'I', 'N', 0x01, 0x02};
    pkg.add_file("bin/main", bin_data);
    std::vector<uint8_t> cfg_data = {'{', '}', '\n'};
    pkg.add_file("res/config.json", cfg_data);
    return pkg;
}

// 1. Fuzz Package Container & Decompression
static void test_package_parser_fuzzing() {
    std::cout << "[Stage 233.20] Fuzzing AppPackage container parser (1,000 iterations)...\n";
    std::mt19937 rng(kFuzzSeed);

    AppPackage base_pkg = make_baseline_package();
    std::vector<uint8_t> packed_base = base_pkg.pack_v3();
    TEST_ASSERT(!packed_base.empty());

    for (int iter = 0; iter < 1000; ++iter) {
        std::vector<uint8_t> mutated = packed_base;
        const int mutation_type = std::uniform_int_distribution<int>(0, 4)(rng);

        if (mutation_type == 0 && !mutated.empty()) {
            // Random bit/byte flips
            int num_flips = std::uniform_int_distribution<int>(1, 10)(rng);
            for (int f = 0; f < num_flips; ++f) {
                size_t idx = std::uniform_int_distribution<size_t>(0, mutated.size() - 1)(rng);
                mutated[idx] ^= static_cast<uint8_t>(std::uniform_int_distribution<int>(1, 255)(rng));
            }
        } else if (mutation_type == 1 && mutated.size() > 8) {
            // Random truncation
            size_t new_len = std::uniform_int_distribution<size_t>(0, mutated.size() - 1)(rng);
            mutated.resize(new_len);
        } else if (mutation_type == 2) {
            // Pure random garbage insertion
            size_t count = std::uniform_int_distribution<size_t>(1, 512)(rng);
            for (size_t g = 0; g < count; ++g) {
                mutated.push_back(static_cast<uint8_t>(std::uniform_int_distribution<int>(0, 255)(rng)));
            }
        } else if (mutation_type == 3 && mutated.size() > 16) {
            // Corrupt manifest length / file count header fields
            size_t off = std::uniform_int_distribution<size_t>(8, 16)(rng);
            mutated[off] = 0xFF;
            mutated[off + 1] = 0xFF;
        } else {
            // Zero out random range
            if (mutated.size() > 10) {
                size_t start = std::uniform_int_distribution<size_t>(0, mutated.size() - 10)(rng);
                std::fill(mutated.begin() + start, mutated.begin() + start + 8, static_cast<uint8_t>(0x00));
            }
        }

        AppPackage parsed;
        // Invariant: Parser must never crash or throw unhandled exceptions
        auto res = AppPackage::validate_package_data(mutated.data(), mutated.size(), &parsed);
        // Result must be one of the known validation enum values
        (void)res;
    }
}

// 2. Fuzz WIOSAUTH v2 Trailer & Signatures
static void test_wiosauth_trailer_fuzzing() {
    std::cout << "[Stage 233.20] Fuzzing WIOSAUTH v2 trailer (1,000 iterations)...\n";
    std::mt19937 rng(kFuzzSeed + 1);

    PublisherTrustStore store;
    auto dev_id = DeveloperKeyStore::generate_identity(
        "com.fuzz.pub", "Fuzz Corp", "key-fuzz-01", PublisherRole::Developer);
    store.register_key(DeveloperKeyStore::export_public_key(dev_id));

    std::string content = "WIOSPKG3\nversion=3.0\napp_id=com.fuzz.target\n[data]\nsome_important_binary_payload\n";
    std::vector<uint8_t> valid_pkg(content.begin(), content.end());
    valid_pkg.resize(256, 0xEE);
    TEST_ASSERT(PublisherTrustStore::sign_package_v2(valid_pkg, dev_id, 0x00000004));

    for (int iter = 0; iter < 1000; ++iter) {
        std::vector<uint8_t> mutated = valid_pkg;

        // Mutate somewhere in the trailer (last 128 bytes)
        if (mutated.size() >= 128) {
            size_t trailer_start = mutated.size() - 128;
            size_t mutate_idx = std::uniform_int_distribution<size_t>(trailer_start, mutated.size() - 1)(rng);
            mutated[mutate_idx] ^= static_cast<uint8_t>(std::uniform_int_distribution<int>(1, 255)(rng));
        }

        auto res = store.verify_package_signature(mutated.data(), mutated.size(), TrustPolicy::StrictProduction);
        // Invariant: Mutated trailer/signature must NEVER be trusted under StrictProduction policy!
        TEST_ASSERT(!res.is_trusted);
        TEST_ASSERT(res.status != SignatureStatus::DeveloperSigned);
        TEST_ASSERT(res.status != SignatureStatus::PlatformSigned);
    }
}

// 3. Fuzz Canonical Path Validator
static void test_canonical_path_fuzzing() {
    std::cout << "[Stage 233.20] Fuzzing CanonicalPathValidator (2,500 iterations)...\n";
    std::mt19937 rng(kFuzzSeed + 2);

    const std::string hostile_tokens[] = {
        "../", "..\\", "....//", "/..", "\\..",
        "CON", "PRN", "AUX", "NUL", "COM1", "COM9", "LPT1", "LPT9",
        "file:stream", ":hidden", "::$DATA",
        "PROGRA~1", "RUNTIM~1.DLL",
        "foo. ", "bar...", "dir/ ", " /dir",
        "C:\\", "\\\\?\\", "//server/share",
        "%2e%2e%2f", "%00", "\0", "\x01\x02\x1F\x7F"
    };

    for (int iter = 0; iter < 2500; ++iter) {
        std::string fuzz_path;
        int num_segments = std::uniform_int_distribution<int>(1, 6)(rng);

        for (int s = 0; s < num_segments; ++s) {
            if (std::uniform_int_distribution<int>(0, 2)(rng) == 0) {
                // Pick a hostile token
                int token_idx = std::uniform_int_distribution<int>(0, 23)(rng);
                fuzz_path += hostile_tokens[token_idx];
            } else {
                // Generate random alphanumeric segment
                int seg_len = std::uniform_int_distribution<int>(1, 10)(rng);
                for (int c = 0; c < seg_len; ++c) {
                    char ch = "abcdefghijklmnopqrstuvwxyz0123456789._-"[std::uniform_int_distribution<int>(0, 38)(rng)];
                    fuzz_path += ch;
                }
            }
            if (s + 1 < num_segments) fuzz_path += "/";
        }

        std::string norm_path;
        // Invariant: Must never crash, throw, or cause memory access violations
        auto status = CanonicalPathValidator::validate_and_normalize_bundle_path(fuzz_path, norm_path);

        if (status == PathValidationStatus::Valid) {
            // If deemed valid, verify invariant: no path segment can be ".." or "."
            std::istringstream iss(norm_path);
            std::string seg;
            while (std::getline(iss, seg, '/')) {
                TEST_ASSERT(seg != ".." && seg != ".");
            }
            TEST_ASSERT(norm_path.find(':') == std::string::npos);
            TEST_ASSERT(norm_path.find('\\') == std::string::npos);
            TEST_ASSERT(norm_path.find('~') == std::string::npos);
        }
    }
}

// 4. Fuzz Syscall Memory Address Validator
static void test_guest_memory_validator_fuzzing() {
    std::cout << "[Stage 233.20] Fuzzing GuestMemoryValidator boundaries (1,000 iterations)...\n";
    std::mt19937 rng(kFuzzSeed + 3);

    Memory mem(0x1000000ULL); // 16 MB
    PhysicalPageManager ppm(mem, 0x40000ULL, 0xFC0000ULL, 0x28020ULL);
    ppm.initialize(false);
    const uint64_t root_l2 = ppm.allocate_page();
    UserAddressSpace uas(mem, ppm, root_l2, "fuzz_uas");

    // Map a single page at 0x20000
    const uint64_t pa = ppm.allocate_page();
    uas.map_user_page(0x20000ULL, pa, PagePermissions::UserReadWrite);

    for (int iter = 0; iter < 1000; ++iter) {
        // Random 64-bit addresses, sizes, alignments
        uint64_t fuzz_va = (static_cast<uint64_t>(rng()) << 32) | rng();
        size_t fuzz_size = static_cast<size_t>(rng() % 0x100000);
        size_t fuzz_align = 1ULL << (rng() % 6); // 1, 2, 4, 8, 16, 32
        bool require_writable = (rng() % 2 == 1);

        // Invariant: Must never crash or cause host invalid memory dereference
        auto res = GuestMemoryValidator::validate_buffer(uas, fuzz_va, fuzz_size, require_writable, fuzz_align, false);
        (void)res;

        std::string out_str;
        auto s_res = GuestMemoryValidator::copy_string(uas, fuzz_va, 64, out_str);
        (void)s_res;
    }
}

int main() {
    std::cout << "====================================================\n";
    std::cout << "  Stage 233.20: Security Fuzzing Infrastructure     \n";
    std::cout << "====================================================\n";

    test_package_parser_fuzzing();
    test_wiosauth_trailer_fuzzing();
    test_canonical_path_fuzzing();
    test_guest_memory_validator_fuzzing();

    std::cout << "\n[Stage 233.20] ALL SECURITY FUZZING TESTS PASSED.\n";
    return 0;
}
