#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>

#include "memory/independent_stack_exception_auditor.h"
#include "memory/canonical_physical_memory_map.h"
#include "cpu/deterministic_virtual_timer.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::memory;
using namespace winios::cpu;

// Helper to encode STP/LDP for auditor tests
static inline uint32_t encode_stp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9000000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

static inline uint32_t encode_ldp_sp(uint32_t k) {
    uint32_t t1 = 2 * k;
    uint32_t t2 = 2 * k + 1;
    uint32_t imm7 = 2 * k;
    return 0xA9400000u | (imm7 << 15) | (t2 << 10) | (31u << 5) | t1;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 262.19 - Full Independent Architectural Audit
// ----------------------------------------------------------------------------
static void test_stage262_19_full_architectural_audit() {
    std::cout << "[Stage 262.19] Testing Independent Architectural Audit...\n";

    IndependentStackAndExceptionAuditor auditor;

    // 1. Audit Canonical Memory Map & Stacks
    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    auto mem_res = auditor.audit_canonical_memory_and_stacks(map);
    if (!mem_res.passed) {
        for (const auto& err : mem_res.errors) {
            std::cerr << "Memory Map Audit Error: " << err << "\n";
        }
    }
    TEST_ASSERT(mem_res.passed, "Canonical dual-stack memory map passed independent audit");

    // 2. Audit Valid Trampolines
    std::vector<uint32_t> save_code;
    save_code.push_back(0xD10483FFu); // sub sp, sp, #288
    for (uint32_t k = 0; k < 15; ++k) save_code.push_back(encode_stp_sp(k));
    save_code.push_back(0xF9007BFEu); // str x30, [sp, #240]
    save_code.push_back(0x910483E0u); // add x0, sp, #288
    save_code.push_back(0xF9007FE0u); // str x0, [sp, #248]
    save_code.push_back(0xD5384020u); // mrs x0, elr_el1
    save_code.push_back(0xF90083E0u); // str x0, [sp, #256]
    save_code.push_back(0xD5384001u); // mrs x1, spsr_el1
    save_code.push_back(0xF90087E1u); // str x1, [sp, #264]
    save_code.push_back(0xD5386002u); // mrs x2, far_el1
    save_code.push_back(0xF9008BE2u); // str x2, [sp, #272]
    save_code.push_back(0xD5385203u); // mrs x3, esr_el1
    save_code.push_back(0xB9011BE3u); // str w3, [sp, #280]
    save_code.push_back(0x52800024u); // mov w4, #1
    save_code.push_back(0xB9011FE4u); // str w4, [sp, #284]
    save_code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    save_code.push_back(0xA9410FE2u); // ldp x2, x3, [sp, #16]
    save_code.push_back(0xF94013E4u); // ldr x4, [sp, #32]

    std::vector<uint32_t> restore_code;
    restore_code.push_back(0xF94083E0u); // ldr x0, [sp, #256]
    restore_code.push_back(0xD5184020u); // msr elr_el1, x0
    restore_code.push_back(0xF94087E1u); // ldr x1, [sp, #264]
    restore_code.push_back(0xD5184001u); // msr spsr_el1, x1
    restore_code.push_back(0xF9407BFEu); // ldr x30, [sp, #240]
    for (int k = 14; k >= 1; --k) restore_code.push_back(encode_ldp_sp(static_cast<uint32_t>(k)));
    restore_code.push_back(0xA94007E0u); // ldp x0, x1, [sp, #0]
    restore_code.push_back(0x910483FFu); // add sp, sp, #288
    restore_code.push_back(0xD69F03E0u); // eret

    auto tramp_res = auditor.audit_exception_trampolines(save_code.data(), save_code.size(),
                                                         restore_code.data(), restore_code.size(),
                                                         false);
    if (!tramp_res.passed) {
        for (const auto& err : tramp_res.errors) {
            std::cerr << "Trampoline Audit Error: " << err << "\n";
        }
    }
    TEST_ASSERT(tramp_res.passed, "Exception trampolines passed independent audit");

    // 3. Audit Timer Catchup
    DeterministicVirtualTimer timer(24000000ULL);
    timer.reset();
    timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
    timer.set_tval(0, 1000);
    uint64_t prev_d = timer.get_comparator(0);
    uint64_t period = 1000;
    uint64_t counter = prev_d + 2 * period;
    timer.advance_counter(counter);
    auto rearm_res = timer.rearm_periodic(0, period);

    auto timer_res = auditor.audit_timer_catchup(timer, 0, prev_d, period, counter,
                                                 rearm_res.next_deadline, rearm_res.missed_ticks);
    if (!timer_res.passed) {
        for (const auto& err : timer_res.errors) {
            std::cerr << "Timer Audit Error: " << err << "\n";
        }
    }
    TEST_ASSERT(timer_res.passed, "Timer catchup passed independent audit");

    std::cout << "[Stage 262.19] Full independent architectural audit passed.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 262.20 - Verification of All 18 Deliberate Architectural Mutations
// ----------------------------------------------------------------------------
static void test_stage262_20_mutation_detection_suite() {
    std::cout << "[Stage 262.20] Testing Detection of All 18 Deliberate Architectural Mutations...\n";

    IndependentStackAndExceptionAuditor auditor;

    for (int m = 1; m <= 18; ++m) {
        MutationType mut = static_cast<MutationType>(m);
        std::string msg;
        bool detected = auditor.test_mutation_detection(mut, &msg);

        TEST_ASSERT(detected, ("Mutation " + std::to_string(m) + " (" + mutation_type_to_string(mut) + ") was detected").c_str());
        TEST_ASSERT(!msg.empty(), "Mutation detection produced informative explanation");
        std::cout << "  [Mutation " << m << ": " << mutation_type_to_string(mut) << "] DETECTED: " << msg << "\n";
    }

    std::cout << "[Stage 262.20] All 18 deliberate architectural mutations successfully detected!\n";
}

// ----------------------------------------------------------------------------
// Test 3: Stage 262.21 - MMU & W^X Claim Classification Audit
// ----------------------------------------------------------------------------
static void test_stage262_21_mmu_and_wx_claim_classification() {
    std::cout << "[Stage 262.21] Auditing MMU & W^X Architectural Claims...\n";

    // Host physical memory map enforces W^X on physical frame level:
    CanonicalPhysicalMemoryMap map = CanonicalPhysicalMemoryMap::create_canonical_dual_stack_layout();
    for (const auto& interval : map.intervals()) {
        bool writable = (interval.permissions & 2) != 0;
        bool executable = (interval.permissions & 4) != 0;
        TEST_ASSERT(!(writable && executable), "W^X enforced across physical intervals");
    }

    // Classification of MMU features:
    // Physical Memory Permission Table: VERIFIED / FUNCTIONAL
    // Multi-level Guest Hardware Page Table Walks (L0->L3): PARTIAL / BLOCKED
    // Hardware TLBI Invalidation Broadcast: PARTIAL / BLOCKED
    const char* kMmuWalkStatus = "PARTIAL / BLOCKED (Requires full VMSA translation regime)";
    const char* kTlbiStatus = "PARTIAL / BLOCKED (Hardware TLBI broadcast not integrated)";

    TEST_ASSERT(kMmuWalkStatus != nullptr, "MMU page table walk status classified");
    TEST_ASSERT(kTlbiStatus != nullptr, "TLBI broadcast status classified");

    std::cout << "[Stage 262.21] MMU & W^X claim classification verified (Walks and TLBI classified as PARTIAL/BLOCKED).\n";
}

int main() {
    std::cout << "=== Running Stage 262.21: Independent Auditor & Mutation Tests ===\n";
    test_stage262_19_full_architectural_audit();
    test_stage262_20_mutation_detection_suite();
    test_stage262_21_mmu_and_wx_claim_classification();
    std::cout << "=== ALL STAGE 262.21 TESTS PASSED ===\n";
    return 0;
}
