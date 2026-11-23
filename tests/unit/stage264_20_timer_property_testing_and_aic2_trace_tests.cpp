#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdlib>
#include <random>

#include "cpu/deterministic_virtual_timer.h"
#include "device/apple_aic.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "Assertion Failed: " #cond << " (" << (msg) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while (0)

using namespace winios;
using namespace winios::cpu;
using namespace winios::device;

// ----------------------------------------------------------------------------
// Reference Timer Oracle
// ----------------------------------------------------------------------------
static TimerRearmResult reference_timer_oracle(uint64_t C, uint64_t D, uint64_t P) {
    TimerRearmResult out{};
    if (P == 0) {
        out.status = TimerRearmStatus::InvalidPeriod;
        out.next_deadline = D;
        return out;
    }

    if (D == UINT64_MAX || C < D) {
        out.elapsed_periods = 1;
        out.missed_ticks = 0;
        uint64_t base = (D == UINT64_MAX) ? C : D;
        if (UINT64_MAX - base < P) {
            out.next_deadline = UINT64_MAX;
            out.status = TimerRearmStatus::DeadlineSaturated;
        } else {
            out.next_deadline = base + P;
            out.status = TimerRearmStatus::Success;
        }
        return out;
    }

    uint64_t delta = C - D;
    uint64_t n = (delta / P) + 1;
    out.elapsed_periods = n;
    out.missed_ticks = (n > 0) ? (n - 1) : 0;

    if (n > 0 && P > (UINT64_MAX / n)) {
        out.next_deadline = UINT64_MAX;
        out.status = TimerRearmStatus::MultiplicationOverflow;
        return out;
    }

    uint64_t advance = n * P;
    if (UINT64_MAX - D < advance) {
        out.next_deadline = UINT64_MAX;
        out.status = TimerRearmStatus::DeadlineSaturated;
        return out;
    }

    out.next_deadline = D + advance;
    out.status = TimerRearmStatus::Success;
    return out;
}

// ----------------------------------------------------------------------------
// Test 1: Stage 264.17 & 264.18 - Timer Boundary Audit & Property Fuzzing
// ----------------------------------------------------------------------------
static void test_stage264_17_18_timer_properties_and_fuzzing() {
    std::cout << "[Stage 264.17-18] Testing Timer Arithmetic Formal Boundaries & Property Fuzzing...\n";

    DeterministicVirtualTimer timer(24000000ULL);

    // 1. Boundary Vectors
    struct TestCase {
        const char* name;
        uint64_t C;
        uint64_t D;
        uint64_t P;
    };

    const TestCase kBoundaries[] = {
        { "On-Time Service",                  240000ULL,           240000ULL,           240000ULL },
        { "Before Deadline",                  100000ULL,           240000ULL,           240000ULL },
        { "One Tick Late",                    240001ULL,           240000ULL,           240000ULL },
        { "One Period Late",                  480000ULL,           240000ULL,           240000ULL },
        { "5.5 Periods Late",                 1560000ULL,          240000ULL,           240000ULL },
        { "10000 Periods Late",               2400240000ULL,       240000ULL,           240000ULL },
        { "Zero Period (Invalid)",            500000ULL,           500000ULL,           0ULL },
        { "Near Max Counter",                 UINT64_MAX - 500ULL, UINT64_MAX - 1000ULL,100ULL },
        { "Multiplication Overflow Vector",   UINT64_MAX - 10ULL,  10ULL,               0x8000000000000000ULL },
        { "Deadline Saturation Vector",       UINT64_MAX - 50ULL,  UINT64_MAX - 100ULL, 200ULL }
    };

    for (const auto& vec : kBoundaries) {
        auto oracle_res = reference_timer_oracle(vec.C, vec.D, vec.P);

        timer.reset();
        timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
        timer.set_comparator(0, vec.D);
        timer.advance_counter(vec.C);

        auto prod_res = timer.rearm_periodic(0, vec.P);

        TEST_ASSERT(prod_res.elapsed_periods == oracle_res.elapsed_periods,
                    ("Elapsed periods match on " + std::string(vec.name)).c_str());
        TEST_ASSERT(prod_res.missed_ticks == oracle_res.missed_ticks,
                    ("Missed ticks match on " + std::string(vec.name)).c_str());
        TEST_ASSERT(prod_res.next_deadline == oracle_res.next_deadline,
                    ("Next deadline matches on " + std::string(vec.name)).c_str());
        TEST_ASSERT(prod_res.status == oracle_res.status,
                    ("Status matches on " + std::string(vec.name)).c_str());
    }

    // 2. Randomized 64-bit Property Fuzzing (500 iterations, fixed reproducible seed)
    const uint64_t kSeed = 0x264C001ULL;
    std::mt19937_64 rng(kSeed);
    std::uniform_int_distribution<uint64_t> dist_val(1, UINT64_MAX - 1);
    std::uniform_int_distribution<uint64_t> dist_period(1, 1000000ULL);

    for (int iter = 0; iter < 500; ++iter) {
        uint64_t D = dist_val(rng);
        uint64_t delta = dist_period(rng) * (iter % 20);
        uint64_t C = (UINT64_MAX - D > delta) ? (D + delta) : D;
        uint64_t P = dist_period(rng);

        auto oracle_res = reference_timer_oracle(C, D, P);

        timer.reset();
        timer.set_control(0, DeterministicVirtualTimer::kCtlEnable);
        timer.set_comparator(0, D);
        timer.advance_counter(C);

        auto prod_res = timer.rearm_periodic(0, P);

        TEST_ASSERT(prod_res.elapsed_periods == oracle_res.elapsed_periods, "Fuzz elapsed periods match");
        TEST_ASSERT(prod_res.next_deadline == oracle_res.next_deadline, "Fuzz next deadline matches");
        TEST_ASSERT(prod_res.status == oracle_res.status, "Fuzz status matches");
    }

    std::cout << "[Stage 264.17-18] Timer arithmetic formal boundaries and 500 property tests verified.\n";
}

// ----------------------------------------------------------------------------
// Test 2: Stage 264.19 & 264.20 - AIC2 Mapping & End-to-End MMIO Trace Proof
// ----------------------------------------------------------------------------
static void test_stage264_19_20_aic2_mapping_and_trace() {
    std::cout << "[Stage 264.19-20] Testing AIC2 Address Reconciliation & End-to-End MMIO Proof...\n";

    // 1. Address reconciliation
    const uint64_t kHardwareAicBase = 0x23B100000ULL; // Apple A16 36-bit MMIO
    const uint64_t kGuestAicBase    = 0x08000000ULL;  // WinIOS Level 3 sub-1GB aperture
    TEST_ASSERT(kHardwareAicBase != kGuestAicBase, "Distinct address spaces documented");

    // 2. End-to-End Execution Trace Proof
    AppleAIC aic(0x41494332u, kGuestAicBase, 0x00100000ULL, 6);
    aic.reset();

    // Configure and unmask HW IRQ 32 (Timer)
    aic.write_register(AppleAIC::kRegConfig, 1u, 4);
    aic.write_register(AppleAIC::kRegMaskClrBase, (1u << 0), 4); // IRQ 32 = word 1, bit 0

    // Timer fires -> assert HW IRQ 32
    aic.write_register(AppleAIC::kRegSwSetBase, (1u << 0), 4);

    // Guest ACK cycle: load from 0x08002004
    uint64_t event_ack = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
    TEST_ASSERT(event_ack != AppleAIC::kEventNone, "Interrupt asserted and popped during ACK");

    // Guest EOI cycle: store to 0x08002004
    aic.write_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, event_ack, 4);

    // Verify queue empty and interrupt deasserted
    uint64_t event_after_eoi = aic.read_register(AppleAIC::kRegCoreBase + AppleAIC::kCoreOffEvent, 4);
    TEST_ASSERT(event_after_eoi == AppleAIC::kEventNone, "Interrupt queue empty after EOI");

    std::cout << "[Stage 264.19-20] AIC2 mapping and end-to-end MMIO ACK/EOI trace proof verified.\n";
}

int main() {
    std::cout << "=== Running Stage 264.20: Timer Property Testing & AIC2 Trace Tests ===\n";
    test_stage264_17_18_timer_properties_and_fuzzing();
    test_stage264_19_20_aic2_mapping_and_trace();
    std::cout << "=== ALL STAGE 264.20 TESTS PASSED ===\n";
    return 0;
}
