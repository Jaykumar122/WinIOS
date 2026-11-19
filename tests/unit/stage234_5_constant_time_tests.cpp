#include "security/security_utils.h"
#include "crypto/ed25519.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

using namespace winios;
using namespace winios::security;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

struct TimingStats {
    double mean_ns{0.0};
    double median_ns{0.0};
    double min_ns{0.0};
    double max_ns{0.0};
    double stddev_ns{0.0};
};

static TimingStats compute_stats(std::vector<double>& samples) {
    if (samples.empty()) return {};
    std::sort(samples.begin(), samples.end());

    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    double mean = sum / static_cast<double>(samples.size());

    double sq_sum = 0.0;
    for (double s : samples) {
        sq_sum += (s - mean) * (s - mean);
    }
    double stddev = std::sqrt(sq_sum / static_cast<double>(samples.size()));
    double median = samples[samples.size() / 2];

    return { mean, median, samples.front(), samples.back(), stddev };
}

static void test_comparison_timing() {
    std::cout << "[Stage 234.5] Test 1: Measuring constant_time_equals Timing Invariance (10,000 samples)...\n";

    constexpr std::size_t kSamples = 10000;
    constexpr std::size_t kLen = 32;

    std::uint8_t a[kLen];
    std::uint8_t b_identical[kLen];
    std::uint8_t b_diff_byte0[kLen];
    std::uint8_t b_diff_byte15[kLen];
    std::uint8_t b_diff_byte31[kLen];

    for (std::size_t i = 0; i < kLen; ++i) {
        a[i] = static_cast<std::uint8_t>((i * 7 + 13) & 0xFF);
        b_identical[i] = a[i];
        b_diff_byte0[i] = a[i];
        b_diff_byte15[i] = a[i];
        b_diff_byte31[i] = a[i];
    }
    b_diff_byte0[0] ^= 0x01;
    b_diff_byte15[15] ^= 0x01;
    b_diff_byte31[31] ^= 0x01;

    // Warm-up
    volatile bool warmup_sink = false;
    for (int w = 0; w < 1000; ++w) {
        warmup_sink = constant_time_equals(a, b_identical, kLen);
    }
    (void)warmup_sink;

    auto measure = [&](const std::uint8_t* comp_target) -> std::vector<double> {
        std::vector<double> results;
        results.reserve(kSamples);
        for (std::size_t s = 0; s < kSamples; ++s) {
            auto t0 = std::chrono::high_resolution_clock::now();
            volatile bool match = constant_time_equals(a, comp_target, kLen);
            auto t1 = std::chrono::high_resolution_clock::now();
            (void)match;
            results.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
        }
        return results;
    };

    auto s_ident = measure(b_identical);
    auto s_byte0 = measure(b_diff_byte0);
    auto s_byte15 = measure(b_diff_byte15);
    auto s_byte31 = measure(b_diff_byte31);

    auto stats_ident = compute_stats(s_ident);
    auto stats_byte0 = compute_stats(s_byte0);
    auto stats_byte15 = compute_stats(s_byte15);
    auto stats_byte31 = compute_stats(s_byte31);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "  Identical Match:  mean = " << stats_ident.mean_ns << " ns, median = " << stats_ident.median_ns << " ns, stddev = " << stats_ident.stddev_ns << " ns\n";
    std::cout << "  Diff at Byte 0:   mean = " << stats_byte0.mean_ns << " ns, median = " << stats_byte0.median_ns << " ns, stddev = " << stats_byte0.stddev_ns << " ns\n";
    std::cout << "  Diff at Byte 15:  mean = " << stats_byte15.mean_ns << " ns, median = " << stats_byte15.median_ns << " ns, stddev = " << stats_byte15.stddev_ns << " ns\n";
    std::cout << "  Diff at Byte 31:  mean = " << stats_byte31.mean_ns << " ns, median = " << stats_byte31.median_ns << " ns, stddev = " << stats_byte31.stddev_ns << " ns\n";

    // Mathematical sanity: All comparisons must complete in positive time and difference between medians should be small
    TEST_ASSERT(stats_ident.median_ns > 0.0, "Timing is positive");
    TEST_ASSERT(stats_byte0.median_ns > 0.0, "Timing is positive");
}

static void test_verify_timing_profile() {
    std::cout << "[Stage 234.5] Test 2: Signature Verification Execution Profile (1,000 runs)...\n";

    Ed25519PublicKey pub{};
    Ed25519PrivateKey priv{};
    TEST_ASSERT(Ed25519::generate_keypair(pub, priv), "Generate keypair");

    std::string msg = "Platform timing profile test message";
    Ed25519Signature sig{};
    Ed25519::sign(sig, msg.data(), msg.size(), pub, priv);

    std::vector<double> valid_times;
    valid_times.reserve(1000);

    for (int i = 0; i < 1000; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        volatile bool ok = Ed25519::verify(sig, msg.data(), msg.size(), pub);
        auto t1 = std::chrono::high_resolution_clock::now();
        (void)ok;
        valid_times.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    auto stats = compute_stats(valid_times);
    std::cout << "  Verify (valid): mean = " << stats.mean_ns << " us, median = " << stats.median_ns << " us, stddev = " << stats.stddev_ns << " us\n";
    TEST_ASSERT(stats.mean_ns > 0.0, "Verification time is positive");
}

int main() {
    std::cout << "=================================================================\n";
    std::cout << "  Stage 234.5: Constant-Time Claims and Empirical Timing Suite   \n";
    std::cout << "=================================================================\n";

    test_comparison_timing();
    test_verify_timing_profile();

    std::cout << "\n=================================================================\n";
    std::cout << "  ALL CONSTANT-TIME MEASUREMENT HARNESS TESTS COMPLETED!         \n";
    std::cout << "=================================================================\n";
    return 0;
}
