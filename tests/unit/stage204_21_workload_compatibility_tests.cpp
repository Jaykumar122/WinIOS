// ==============================================================================
// WinIOS Campaign 204 Stage 204.21: Public Workload Compatibility Matrix Tests
// ==============================================================================
// Evaluates all 12 controlled clean-room fixtures across the Darwin application
// runtime lifecycle:
// 1. Parsed
// 2. Loaded
// 3. Linked
// 4. Initialized
// 5. Partially executed
// 6. Successfully executed
// 7. Unsupported / Malformed
// ==============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "loader/macho_fixture_corpus.h"
#include "loader/userspace_macho_loader.h"
#include "runtime/darwin_application_runner.h"

using namespace winios;
using namespace winios::loader;
using namespace winios::runtime;

static int g_failures = 0;

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] Line " << __LINE__ << ": " << msg << "\n"; \
            ++g_failures; \
        } \
    } while (0)

enum class WorkloadStatus {
    Unsupported,
    Parsed,
    Loaded,
    Linked,
    Initialized,
    PartiallyExecuted,
    SuccessfullyExecuted
};

static std::string status_to_string(WorkloadStatus st) {
    switch (st) {
        case WorkloadStatus::Unsupported: return "Unsupported";
        case WorkloadStatus::Parsed: return "Parsed";
        case WorkloadStatus::Loaded: return "Loaded";
        case WorkloadStatus::Linked: return "Linked";
        case WorkloadStatus::Initialized: return "Initialized";
        case WorkloadStatus::PartiallyExecuted: return "Partially executed";
        case WorkloadStatus::SuccessfullyExecuted: return "Successfully executed";
    }
    return "Unknown";
}

struct WorkloadEvaluation {
    std::string name;
    std::string kind;
    WorkloadStatus status;
    std::string details;
};

WorkloadEvaluation evaluate_workload(const ControlledMachOFixture& fix) {
    WorkloadEvaluation eval{};
    eval.name = fix.name;
    eval.kind = (fix.filetype == kMachOFileTypeExecute) ? "Executable" : "Dylib";

    // Step 1: Parse check
    UserspaceLoadResult load_res{};
    bool parsed = UserspaceMachOLoader::parse_metadata(
        fix.binary_data.data(), fix.binary_data.size(), load_res);

    if (!parsed) {
        eval.status = WorkloadStatus::Unsupported;
        eval.details = "Header/Command parsing rejected";
        return eval;
    }

    eval.status = WorkloadStatus::Parsed;

    // Check for malformed segment overlap
    if (fix.is_malformed) {
        // Runner should reject malformed images safely during load
        DarwinApplicationRunner runner;
        std::string err;
        bool loaded = runner.load_executable(fix.binary_data.data(), fix.binary_data.size(), err);
        if (!loaded) {
            eval.status = WorkloadStatus::Unsupported;
            eval.details = "Rejected safely during loading: " + err;
            return eval;
        }
    }

    // Step 2: Dylibs vs Executables
    if (fix.filetype == kMachOFileTypeDylib) {
        // Dylibs are tested via library loading & linking
        DarwinApplicationRunner runner;
        std::string err;
        bool lib_loaded = runner.load_library(fix.name, fix.binary_data.data(), fix.binary_data.size(), err);
        if (!lib_loaded) {
            eval.details = "Dylib load failed: " + err;
            return eval;
        }
        eval.status = WorkloadStatus::Loaded;

        bool linked = runner.link_and_initialize(err);
        if (linked) {
            eval.status = WorkloadStatus::Linked;
            eval.details = "Dylib loaded and linked successfully into dyld registry";
        } else {
            eval.details = "Dylib linking had unmet dependencies: " + err;
        }
        return eval;
    }

    // For executables, execute through DarwinApplicationRunner
    ApplicationConfig cfg{};
    cfg.executable_path = "/" + fix.name;
    cfg.max_instructions = 10000;
    cfg.run_initializers = true;

    DarwinApplicationRunner runner(cfg);
    std::string err;

    // If it has dependencies (e.g. multi-image app), load them
    if (fix.name == "fixture_multi_image_app") {
        auto dylib_base = PublicMachOCorpus::create_multi_image_dylib_base();
        auto dylib_math = PublicMachOCorpus::create_multi_image_dylib_math();
        runner.load_library("/usr/lib/libbase.dylib", dylib_base.binary_data.data(), dylib_base.binary_data.size(), err);
        runner.load_library("/usr/lib/libmath.dylib", dylib_math.binary_data.data(), dylib_math.binary_data.size(), err);
    }

    bool loaded = runner.load_executable(fix.binary_data.data(), fix.binary_data.size(), err);
    if (!loaded) {
        eval.details = "Executable load failed: " + err;
        return eval;
    }
    eval.status = WorkloadStatus::Loaded;

    bool linked = runner.link_and_initialize(err);
    if (!linked) {
        eval.details = "Executable link failed: " + err;
        return eval;
    }
    eval.status = WorkloadStatus::Linked;

    bool prepared = runner.prepare_execution(err);
    if (!prepared) {
        eval.details = "Execution preparation failed: " + err;
        return eval;
    }
    eval.status = WorkloadStatus::Initialized;

    ApplicationRunResult run_res = runner.run();
    if (run_res.terminated && run_res.termination_reason == "SYS_exit") {
        eval.status = WorkloadStatus::SuccessfullyExecuted;
        eval.details = "Clean SYS_exit with code " + std::to_string(run_res.exit_code) +
                       ", retired " + std::to_string(run_res.instructions_retired) + " instructions";
    } else if (run_res.instructions_retired > 0) {
        eval.status = WorkloadStatus::PartiallyExecuted;
        eval.details = "Terminated early: " + run_res.termination_reason;
    } else {
        eval.details = "No instructions retired";
    }

    return eval;
}

void test_all_twelve_fixtures() {
    std::cout << "--- Evaluating 12 Public Corpus Workloads ---" << std::endl;
    auto corpus = PublicMachOCorpus::get_full_corpus();
    TEST_CHECK(corpus.size() == 12, "Corpus must have exactly 12 fixtures");

    std::vector<WorkloadEvaluation> evals;
    for (const auto& fix : corpus) {
        auto eval = evaluate_workload(fix);
        evals.push_back(eval);
        std::cout << "  [" << eval.name << "] -> "
                  << status_to_string(eval.status) << " (" << eval.details << ")\n";
    }

    // Assert expected classifications
    for (const auto& eval : evals) {
        if (eval.name == "fixture_minimal_executable") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "Minimal must execute successfully");
        } else if (eval.name == "fixture_multisegment_executable") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "Multisegment must execute successfully");
        } else if (eval.name == "fixture_multi_image_app") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "Multi-image app must execute successfully");
        } else if (eval.name == "fixture_libmath_dylib") {
            TEST_CHECK(eval.status == WorkloadStatus::Linked, "libmath dylib must link");
        } else if (eval.name == "fixture_libbase_dylib") {
            TEST_CHECK(eval.status == WorkloadStatus::Linked, "libbase dylib must link");
        } else if (eval.name == "fixture_symbol_export_dylib") {
            TEST_CHECK(eval.status == WorkloadStatus::Linked, "export dylib must link");
        } else if (eval.name == "fixture_weak_symbol_app") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "weak symbol app must execute successfully");
        } else if (eval.name == "fixture_chained_fixups_app") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "chained fixup app must execute successfully");
        } else if (eval.name == "fixture_tls_app") {
            TEST_CHECK(eval.status == WorkloadStatus::SuccessfullyExecuted, "TLS app must execute successfully");
        } else if (eval.name == "fixture_malformed_truncated_header") {
            TEST_CHECK(eval.status == WorkloadStatus::Unsupported, "Truncated header must be Unsupported");
        } else if (eval.name == "fixture_malformed_overlapping_segments") {
            TEST_CHECK(eval.status == WorkloadStatus::Unsupported, "Overlapping segments must be Unsupported");
        } else if (eval.name == "fixture_malformed_corrupt_load_commands") {
            TEST_CHECK(eval.status == WorkloadStatus::Unsupported, "Corrupt load commands must be Unsupported");
        }
    }
}

int main() {
    std::cout << "=== Running Stage 204.21 Workload Compatibility Tests ===" << std::endl;

    test_all_twelve_fixtures();

    if (g_failures == 0) {
        std::cout << "ALL STAGE 204.21 WORKLOAD COMPATIBILITY TESTS PASSED!" << std::endl;
        return 0;
    } else {
        std::cerr << g_failures << " TEST(S) FAILED!" << std::endl;
        return 1;
    }
}
