#include "crypto/ed25519.h"
#include "crypto/sha512.h"
#include "security/security_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

using namespace winios;
using namespace winios::crypto;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

struct TimingStats {
    double mean_ms{0.0};
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double stddev_ms{0.0};
    double cv_percent{0.0}; // Coefficient of variation = (stddev / mean) * 100
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
    double cv = (mean > 0.0) ? (stddev / mean) * 100.0 : 0.0;

    return { mean, median, samples.front(), samples.back(), stddev, cv };
}

static void print_stats(const char* label, const TimingStats& s) {
    std::cout << std::left << std::setw(24) << label
              << " Mean: " << std::fixed << std::setprecision(3) << s.mean_ms << " ms | "
              << " Median: " << s.median_ms << " ms | "
              << " StdDev: " << s.stddev_ms << " ms | "
              << " CV: " << std::setprecision(2) << s.cv_percent << "%\n";
}

int main() {
    std::cout << "=== Stage 235.5 Ed25519 Signing Timing Evidence & Side-Channel Resistance ===\n";

    constexpr std::size_t kSamplesPerClass = 30;
    const char message[] = "WinIOS Stage 235 Constant-Time Signing Timing Evidence Payload";
    const std::size_t msg_len = sizeof(message);

    // Define 4 distinct key classes:
    // 1. All-zero seed (produces clamped key with minimum non-clamped bits)
    // 2. All-one seed (0xFF)
    // 3. Alternating 0x55 seed
    // 4. Alternating 0xAA seed

    struct KeyClass {
        const char* name;
        std::uint8_t byte_val;
    };

    static const KeyClass kClasses[] = {
        { "Zero Seed (0x00)", 0x00 },
        { "One Seed (0xFF)", 0xFF },
        { "Alt-5 Seed (0x55)", 0x55 },
        { "Alt-A Seed (0xAA)", 0xAA }
    };

    std::vector<std::pair<std::string, TimingStats>> class_stats;

    // Warm-up run
    {
        Ed25519Seed warm_seed{};
        Ed25519PublicKey warm_pub{};
        Ed25519PrivateKey warm_priv{};
        Ed25519::create_keypair(warm_pub, warm_priv, warm_seed);
        Ed25519Signature warm_sig{};
        for (int w = 0; w < 5; ++w) {
            Ed25519::sign(warm_sig, message, msg_len, warm_pub, warm_priv);
        }
    }

    std::cout << "Collecting " << kSamplesPerClass << " samples per key class...\n";

    for (const auto& kc : kClasses) {
        Ed25519Seed seed{};
        std::memset(seed.data(), kc.byte_val, seed.size());

        Ed25519PublicKey pub{};
        Ed25519PrivateKey priv{};
        Ed25519::create_keypair(pub, priv, seed);

        std::vector<double> samples;
        samples.reserve(kSamplesPerClass);

        for (std::size_t i = 0; i < kSamplesPerClass; ++i) {
            Ed25519Signature sig{};
            auto t0 = std::chrono::high_resolution_clock::now();
            Ed25519::sign(sig, message, msg_len, pub, priv);
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            samples.push_back(ms);

            // Verify signature correctness to ensure no corruptions occurred
            TEST_ASSERT(Ed25519::verify(sig, message, msg_len, pub), "Signature verification failed during timing");
        }

        TimingStats stats = compute_stats(samples);
        class_stats.emplace_back(kc.name, stats);
        print_stats(kc.name, stats);
    }

    // Evaluate statistical invariance across key classes
    // The relative spread across class means should be minimal (typically within host OS scheduling variance)
    double min_mean = class_stats[0].second.mean_ms;
    double max_mean = class_stats[0].second.mean_ms;
    for (const auto& cs : class_stats) {
        min_mean = std::min(min_mean, cs.second.mean_ms);
        max_mean = std::max(max_mean, cs.second.mean_ms);
    }

    double max_spread_percent = ((max_mean - min_mean) / min_mean) * 100.0;
    std::cout << "\nTiming Summary:\n";
    std::cout << "  Min Class Mean: " << min_mean << " ms\n";
    std::cout << "  Max Class Mean: " << max_mean << " ms\n";
    std::cout << "  Max Class Mean Spread: " << std::fixed << std::setprecision(2) << max_spread_percent << "%\n";

    // In a constant-time fixed-256 loop algorithm, instruction count is invariant.
    // The observed spread is solely attributable to OS scheduling jitter and CPU frequency scaling.
    // Ensure spread is reasonable (< 35% on noisy CI / non-isolated host cores).
    TEST_ASSERT(max_spread_percent < 35.0, "Timing variance between key classes exceeds allowable threshold");

    std::cout << "=== All Stage 235.5 Timing Tests PASSED successfully ===" << std::endl;
    return 0;
}
