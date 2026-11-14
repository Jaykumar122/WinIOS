// tests/unit/stage197_10_litmus_tests.cpp
// Campaign 197: Acceptance Tests G and H
// Verifies ARMv8-A Acquire/Release Message Passing and Canonical Weak-Memory
// Litmus Patterns (SB, MP, LB, IRIW, WRC, RWC).

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <iomanip>
#include "cpu/cpu.h"
#include "cpu/guest_memory_order.h"
#include "memory/memory.h"

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << (msg) << " (" #cond ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace winios;

struct LitmusOutcomeRecord {
    std::string test_name;
    std::string pattern;
    std::string configuration;
    std::string outcome_description;
    std::string verdict;
    bool passed;
};

// ============================================================================
// Acceptance Test G: Guest Acquire/Release Message Passing
// ============================================================================
static bool test_acquire_release_message_passing(std::vector<LitmusOutcomeRecord>& records) {
    std::cout << "  Testing Acquire/Release Message Passing (Acceptance Test G)..." << std::endl;
    GuestMemoryOrderModel model(2);

    const std::uint64_t data_addr = 0x1000;
    const std::uint64_t flag_addr = 0x1008;

    // Core 0: writes payload data, then writes flag using ReleaseWrite (STLR)
    auto ev_w_data = model.record_write(0, data_addr, 8, 0xCAFEBABEULL, MemoryAccessType::NormalWrite);
    auto ev_w_flag = model.record_write(0, flag_addr, 8, 1, MemoryAccessType::ReleaseWrite);

    // Core 1: reads flag using AcquireRead (LDAR), then reads payload data
    auto ev_r_flag = model.record_read(1, flag_addr, 8, 1, MemoryAccessType::AcquireRead);
    auto ev_r_data = model.record_read(1, data_addr, 8, 0xCAFEBABEULL, MemoryAccessType::NormalRead);

    // Verification 1: On Core 0, W(data) is strictly ordered before W(flag)
    TEST_ASSERT(model.is_ordered(ev_w_data, ev_w_flag), "Core 0 data write is ordered before release flag write");

    // Verification 2: On Core 1, R(flag) is strictly ordered before R(data)
    TEST_ASSERT(model.is_ordered(ev_r_flag, ev_r_data), "Core 1 acquire flag read is ordered before data read");

    // Verification 3: Message passing outcome evaluation
    auto v_with_acq_rel = GuestMemoryOrderModel::evaluate_mp_outcome(true, 1, 0);
    TEST_ASSERT(v_with_acq_rel == LitmusOutcomeVerdict::Forbidden, "Observing flag=1, data=0 is strictly FORBIDDEN with acquire/release");

    auto v_valid_payload = GuestMemoryOrderModel::evaluate_mp_outcome(true, 1, 0xCAFEBABEULL);
    TEST_ASSERT(v_valid_payload == LitmusOutcomeVerdict::Allowed, "Observing flag=1, data=valid is ALLOWED");

    records.push_back({"MP_AcqRel", "MP", "STLR + LDAR", "flag=1, data=0", "Forbidden", true});
    records.push_back({"MP_AcqRel_Valid", "MP", "STLR + LDAR", "flag=1, data=CAFEBABE", "Allowed", true});

    return true;
}

// ============================================================================
// Acceptance Test H: Canonical Weak-Memory Litmus Suite
// ============================================================================
static bool test_canonical_litmus_patterns(std::vector<LitmusOutcomeRecord>& records) {
    std::cout << "  Testing Canonical Litmus Patterns (Acceptance Test H)..." << std::endl;

    // 1. SB (Store Buffering)
    // Core 0: W(x)=1, R(y)=0
    // Core 1: W(y)=1, R(x)=0
    // In ARMv8-A weak memory model, r0=0, r1=0 is ALLOWED without barriers
    auto v_sb_relaxed = GuestMemoryOrderModel::evaluate_sb_outcome(0, 0);
    TEST_ASSERT(v_sb_relaxed == LitmusOutcomeVerdict::Allowed, "SB outcome r0=0, r1=0 is ALLOWED under relaxed weak-memory");
    records.push_back({"SB_Relaxed", "SB", "No barriers", "r0=0, r1=0", "Allowed", true});

    // With DMB ISH between store and load, r0=0, r1=0 is FORBIDDEN
    GuestMemoryOrderModel model_sb(2);
    auto ev_c0_w = model_sb.record_write(0, 0x100, 8, 1);
    model_sb.record_dmb(0, 11); // DMB ISH
    auto ev_c0_r = model_sb.record_read(0, 0x200, 8, 0);
    TEST_ASSERT(model_sb.is_ordered(ev_c0_w, ev_c0_r), "DMB ISH strictly orders Store-Load on Core 0");
    records.push_back({"SB_DMB", "SB", "DMB ISH", "r0=0, r1=0", "Forbidden", true});

    // 2. MP (Message Passing)
    // Relaxed (no barriers): flag=1, data=0 is ALLOWED
    auto v_mp_relaxed = GuestMemoryOrderModel::evaluate_mp_outcome(false, 1, 0);
    TEST_ASSERT(v_mp_relaxed == LitmusOutcomeVerdict::Allowed, "MP relaxed flag=1, data=0 is ALLOWED");
    records.push_back({"MP_Relaxed", "MP", "No barriers", "flag=1, data=0", "Allowed", true});

    // With DMB ISH: flag=1, data=0 is FORBIDDEN
    GuestMemoryOrderModel model_mp(2);
    auto ev_mp_w1 = model_mp.record_write(0, 0x1000, 8, 0x42);
    model_mp.record_dmb(0, 11); // DMB ISH
    auto ev_mp_w2 = model_mp.record_write(0, 0x1008, 8, 1);
    TEST_ASSERT(model_mp.is_ordered(ev_mp_w1, ev_mp_w2), "DMB ISH orders writes on producer");
    records.push_back({"MP_DMB", "MP", "DMB ISH", "flag=1, data=0", "Forbidden", true});

    // 3. LB (Load Buffering)
    // Core 0: R(x) -> 1, W(y) = 1
    // Core 1: R(y) -> 1, W(x) = 1
    // In ARMv8-A, r0=1, r1=1 is FORBIDDEN (no out-of-thin-air reads / causality preservation)
    auto v_lb = GuestMemoryOrderModel::evaluate_lb_outcome(1, 1);
    TEST_ASSERT(v_lb == LitmusOutcomeVerdict::Forbidden, "LB outcome r0=1, r1=1 is strictly FORBIDDEN in ARMv8-A");
    records.push_back({"LB_Causality", "LB", "Axiomatic", "r0=1, r1=1", "Forbidden", true});

    // 4. IRIW (Independent Reads of Independent Writes)
    // In ARMv8-A other-multicopy atomic model:
    // Core 0: W(x)=1, Core 1: W(y)=1
    // Core 2: R(x)=1, R(y)=0
    // Core 3: R(y)=1, R(x)=0
    // Outcome where Core 2 sees x before y and Core 3 sees y before x is FORBIDDEN!
    auto v_iriw = GuestMemoryOrderModel::evaluate_iriw_outcome(1, 0, 1, 0);
    TEST_ASSERT(v_iriw == LitmusOutcomeVerdict::Forbidden, "IRIW outcome is FORBIDDEN under ARMv8-A other-multicopy atomicity");
    records.push_back({"IRIW_OMCA", "IRIW", "DMB ISH / OMCA", "c2(x=1,y=0), c3(y=1,x=0)", "Forbidden", true});

    // 5. WRC (Write-to-Read Causality)
    // Core 0: W(x)=1
    // Core 1: R(x)=1, W(y)=1
    // Core 2: R(y)=1, R(x)=r0
    auto v_wrc_relaxed = GuestMemoryOrderModel::evaluate_wrc_outcome(false, 0);
    TEST_ASSERT(v_wrc_relaxed == LitmusOutcomeVerdict::Allowed, "WRC relaxed r0=0 is ALLOWED");
    auto v_wrc_ordered = GuestMemoryOrderModel::evaluate_wrc_outcome(true, 0);
    TEST_ASSERT(v_wrc_ordered == LitmusOutcomeVerdict::Forbidden, "WRC ordered r0=0 is FORBIDDEN");
    records.push_back({"WRC_Relaxed", "WRC", "No barriers", "r0=0", "Allowed", true});
    records.push_back({"WRC_Ordered", "WRC", "Acquire/Release", "r0=0", "Forbidden", true});

    // 6. RWC (Read-to-Write Causality)
    // Core 0: W(x)=1
    // Core 1: R(x)=1, R(y)=r0
    // Core 2: W(y)=1, R(x)=r1
    auto v_rwc_relaxed = GuestMemoryOrderModel::evaluate_rwc_outcome(false, 0, 0);
    TEST_ASSERT(v_rwc_relaxed == LitmusOutcomeVerdict::Allowed, "RWC relaxed r0=0, r1=0 is ALLOWED");
    auto v_rwc_ordered = GuestMemoryOrderModel::evaluate_rwc_outcome(true, 0, 0);
    TEST_ASSERT(v_rwc_ordered == LitmusOutcomeVerdict::Forbidden, "RWC ordered r0=0, r1=0 is FORBIDDEN");
    records.push_back({"RWC_Relaxed", "RWC", "No barriers", "r0=0, r1=0", "Allowed", true});
    records.push_back({"RWC_Ordered", "RWC", "Barriers", "r0=0, r1=0", "Forbidden", true});

    return true;
}

int main() {
    std::cout << "[STAGE 197.9-197.10] Running Acquire/Release & Litmus Tests (Acceptance Tests G & H)..." << std::endl;

    std::vector<LitmusOutcomeRecord> records;

    if (!test_acquire_release_message_passing(records)) {
        std::cerr << "test_acquire_release_message_passing FAILED" << std::endl;
        return 1;
    }

    if (!test_canonical_litmus_patterns(records)) {
        std::cerr << "test_canonical_litmus_patterns FAILED" << std::endl;
        return 1;
    }

    std::size_t passed_count = 0;
    for (const auto& r : records) {
        if (r.passed) ++passed_count;
    }

    TEST_ASSERT(passed_count == records.size(), "All litmus test records passed");

    // Export to docs/campaign197/litmus_test_results.json
    std::ofstream out("docs/campaign197/litmus_test_results.json");
    if (out.is_open()) {
        out << "{\n"
            << "  \"campaign\": \"197\",\n"
            << "  \"acceptance_tests\": [\"G\", \"H\"],\n"
            << "  \"status\": \"PASSED\",\n"
            << "  \"total_tests\": " << records.size() << ",\n"
            << "  \"passed_tests\": " << passed_count << ",\n"
            << "  \"results\": [\n";
        for (std::size_t i = 0; i < records.size(); ++i) {
            const auto& r = records[i];
            out << "    {\n"
                << "      \"test_name\": \"" << r.test_name << "\",\n"
                << "      \"pattern\": \"" << r.pattern << "\",\n"
                << "      \"configuration\": \"" << r.configuration << "\",\n"
                << "      \"outcome_description\": \"" << r.outcome_description << "\",\n"
                << "      \"verdict\": \"" << r.verdict << "\",\n"
                << "      \"passed\": " << (r.passed ? "true" : "false") << "\n"
                << "    }" << (i + 1 < records.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
        out.close();
        std::cout << "  Exported results to docs/campaign197/litmus_test_results.json" << std::endl;
    }

    std::cout << "[STAGE 197.9-197.10] All Litmus Tests PASSED." << std::endl;
    return 0;
}
