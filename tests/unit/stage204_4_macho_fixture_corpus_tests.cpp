// ==============================================================================
// WinIOS Campaign 204 Stage 204.4: Public Mach-O Fixture Corpus Tests
// ==============================================================================
// Independent verification of the controlled public Mach-O fixture corpus:
// 1. Minimal executable fixture structure & parsing
// 2. Multi-segment executable (PAGEZERO, TEXT, DATA, LINKEDIT)
// 3. Multi-image dependency chain (app -> libmath -> libbase)
// 4. Symbol export fixture with LC_DYLD_EXPORTS_TRIE
// 5. Weak-symbol binding fixture with LC_LOAD_WEAK_DYLIB
// 6. Chained-fixup fixture with LC_DYLD_CHAINED_FIXUPS
// 7. Thread-local storage (TLS) section fixture (__thread_vars)
// 8. Controlled malformed fixtures (truncated header, overlap, corrupt cmd size)
// 9. Documented provenance, licenses, and verified SHA-256 digests
// ==============================================================================

#include <cstdint>
#include <iostream>
#include <vector>

#include "loader/macho_fixture_corpus.h"
#include "loader/real_artifact_harness.h"
#include "loader/userspace_macho_loader.h"

using namespace winios;
using namespace winios::loader;

namespace {

int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

void test_fixture_catalog_integrity() {
    std::cout << "[Stage 204.4 Test 1] Controlled Fixture Catalog Provenance & Digests...\n";

    auto fixtures = PublicMachOCorpus::get_full_corpus();
    TEST_CHECK(fixtures.size() >= 12, "Corpus must provide at least 12 distinct test fixtures");

    for (const auto& fix : fixtures) {
        TEST_CHECK(!fix.name.empty(), "Fixture name must not be empty");
        TEST_CHECK(!fix.description.empty(), "Fixture description must be documented");
        TEST_CHECK(!fix.provenance.empty(), "Fixture provenance must be documented");
        TEST_CHECK(!fix.license.empty(), "Fixture license must be documented");
        TEST_CHECK(!fix.sha256.empty(), "Fixture SHA-256 must be computed");
        TEST_CHECK(fix.file_size == fix.binary_data.size(), "Recorded size must match actual buffer length");

        // Verify SHA-256 authenticity independently
        std::string computed_hash = RealArtifactHarness::compute_sha256(
            fix.binary_data.data(), fix.binary_data.size());
        TEST_CHECK(computed_hash == fix.sha256, "Fixture SHA-256 must match re-computed digest");
    }

    std::cout << "  -> All 12 fixtures passed provenance and SHA-256 integrity.\n";
}

void test_valid_fixtures_parsing() {
    std::cout << "[Stage 204.4 Test 2] Valid Mach-O Fixtures Structural Parsing...\n";

    // 1. Minimal executable
    auto min_fix = PublicMachOCorpus::create_minimal_executable();
    UserspaceLoadResult min_res{};
    bool min_parsed = UserspaceMachOLoader::parse_metadata(
        min_fix.binary_data.data(), min_fix.binary_data.size(), min_res);
    TEST_CHECK(min_parsed, "Minimal executable must parse successfully");
    TEST_CHECK(min_res.entry_point == 0x0000000100001000ULL, "Entry point must match 0x100001000");
    TEST_CHECK(min_res.find_segment("__TEXT") != nullptr, "Must contain __TEXT segment");

    // 2. Multi-segment executable
    auto multi_fix = PublicMachOCorpus::create_multisegment_executable();
    UserspaceLoadResult multi_res{};
    bool multi_parsed = UserspaceMachOLoader::parse_metadata(
        multi_fix.binary_data.data(), multi_fix.binary_data.size(), multi_res);
    TEST_CHECK(multi_parsed, "Multi-segment executable must parse successfully");
    TEST_CHECK(multi_res.find_segment("__PAGEZERO") != nullptr, "Must contain __PAGEZERO");
    TEST_CHECK(multi_res.find_segment("__TEXT") != nullptr, "Must contain __TEXT");
    TEST_CHECK(multi_res.find_segment("__DATA") != nullptr, "Must contain __DATA");
    TEST_CHECK(multi_res.find_segment("__LINKEDIT") != nullptr, "Must contain __LINKEDIT");

    // 3. Multi-image app
    auto app_fix = PublicMachOCorpus::create_multi_image_app();
    UserspaceLoadResult app_res{};
    bool app_parsed = UserspaceMachOLoader::parse_metadata(
        app_fix.binary_data.data(), app_fix.binary_data.size(), app_res);
    TEST_CHECK(app_parsed, "Multi-image app must parse successfully");
    TEST_CHECK(app_res.dylibs.size() == 1, "App must record 1 dylib dependency");
    TEST_CHECK(app_res.dylibs[0].path == "/usr/lib/libmath.dylib", "Dependency must be /usr/lib/libmath.dylib");

    // 4. Symbol export dylib
    auto export_fix = PublicMachOCorpus::create_symbol_export_dylib();
    UserspaceLoadResult export_res{};
    bool export_parsed = UserspaceMachOLoader::parse_metadata(
        export_fix.binary_data.data(), export_fix.binary_data.size(), export_res);
    TEST_CHECK(export_parsed, "Export dylib must parse successfully");
    TEST_CHECK(export_res.header.filetype == kMachOFileTypeDylib, "Must be MH_DYLIB");

    // 5. Weak symbol app
    auto weak_fix = PublicMachOCorpus::create_weak_symbol_executable();
    UserspaceLoadResult weak_res{};
    bool weak_parsed = UserspaceMachOLoader::parse_metadata(
        weak_fix.binary_data.data(), weak_fix.binary_data.size(), weak_res);
    TEST_CHECK(weak_parsed, "Weak symbol app must parse successfully");
    TEST_CHECK(weak_res.dylibs.size() == 1 && weak_res.dylibs[0].is_weak, "Must flag weak dependency");

    // 6. Chained fixup app
    auto chain_fix = PublicMachOCorpus::create_chained_fixup_executable();
    UserspaceLoadResult chain_res{};
    bool chain_parsed = UserspaceMachOLoader::parse_metadata(
        chain_fix.binary_data.data(), chain_fix.binary_data.size(), chain_res);
    TEST_CHECK(chain_parsed, "Chained fixup executable must parse successfully");
    TEST_CHECK(chain_res.is_arm64e, "Must be identified as ARM64e");

    // 7. TLS app
    auto tls_fix = PublicMachOCorpus::create_tls_executable();
    UserspaceLoadResult tls_res{};
    bool tls_parsed = UserspaceMachOLoader::parse_metadata(
        tls_fix.binary_data.data(), tls_fix.binary_data.size(), tls_res);
    TEST_CHECK(tls_parsed, "TLS executable must parse successfully");
    TEST_CHECK(tls_res.find_segment("__DATA") != nullptr, "Must contain __DATA with TLS");

    std::cout << "  -> Valid fixtures parsed and verified.\n";
}

void test_malformed_fixtures_rejection() {
    std::cout << "[Stage 204.4 Test 3] Malformed Fixtures Controlled Rejection...\n";

    // 1. Truncated header
    auto trunc_fix = PublicMachOCorpus::create_malformed_truncated_header();
    UserspaceLoadResult trunc_res{};
    bool trunc_parsed = UserspaceMachOLoader::parse_metadata(
        trunc_fix.binary_data.data(), trunc_fix.binary_data.size(), trunc_res);
    TEST_CHECK(!trunc_parsed, "Truncated header must fail parsing");
    TEST_CHECK(trunc_res.status == UserspaceMachOLoadStatus::TruncatedHeader,
               "Status must indicate TruncatedHeader");

    // 2. Overlapping segments
    auto overlap_fix = PublicMachOCorpus::create_malformed_overlapping_segments();
    UserspaceLoadResult overlap_res{};
    (void)UserspaceMachOLoader::parse_metadata(
        overlap_fix.binary_data.data(), overlap_fix.binary_data.size(), overlap_res);
    std::string err;
    auto val_status = UserspaceMachOLoader::validate_segments(
        overlap_res.segments, overlap_fix.file_size, kApplePageSize16K, err);
    TEST_CHECK(val_status == UserspaceMachOLoadStatus::OverlappingSegments,
               "Overlapping segments must be caught by validate_segments");

    // 3. Corrupted load commands
    auto corrupt_fix = PublicMachOCorpus::create_malformed_corrupt_load_commands();
    UserspaceLoadResult corrupt_res{};
    bool corrupt_parsed = UserspaceMachOLoader::parse_metadata(
        corrupt_fix.binary_data.data(), corrupt_fix.binary_data.size(), corrupt_res);
    TEST_CHECK(!corrupt_parsed, "Corrupt load commands must fail parsing");
    TEST_CHECK(corrupt_res.status == UserspaceMachOLoadStatus::CorruptedLoadCommands,
               "Status must indicate CorruptedLoadCommands");

    std::cout << "  -> Malformed fixtures rejected safely.\n";
}

} // namespace

int main() {
    std::cout << "=== WinIOS Stage 204.4 Public Mach-O Fixture Corpus Tests ===\n";
    test_fixture_catalog_integrity();
    test_valid_fixtures_parsing();
    test_malformed_fixtures_rejection();

    if (g_failures == 0) {
        std::cout << "[PASS] Stage 204.4 Public Mach-O Fixture Corpus passed all tests.\n";
        return 0;
    } else {
        std::cerr << "[FAIL] Stage 204.4 Public Mach-O Fixture Corpus failed with " << g_failures << " errors.\n";
        return 1;
    }
}
