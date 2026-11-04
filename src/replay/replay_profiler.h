#pragma once

#include "replay/replay_types.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace winios::replay {

// -----------------------------------------------------------------------------
// ReplayProfilePhase: Aggregated timing and call counts for one replay phase
// -----------------------------------------------------------------------------
struct ReplayProfilePhase {
    std::uint64_t calls{0};
    double total_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};

    void record(double elapsed_ms) noexcept {
        if (elapsed_ms < 0.0) {
            elapsed_ms = 0.0;
        }
        if (calls == 0 || elapsed_ms < min_ms) {
            min_ms = elapsed_ms;
        }
        if (calls == 0 || elapsed_ms > max_ms) {
            max_ms = elapsed_ms;
        }
        total_ms += elapsed_ms;
        ++calls;
    }

    [[nodiscard]] double average_ms() const noexcept {
        return calls == 0 ? 0.0 : total_ms / static_cast<double>(calls);
    }
};

// -----------------------------------------------------------------------------
// ReplayProfileScope: Opt-in RAII wall-clock timer
//
// Constructed with a null phase it performs no clock read at all, which keeps
// the disabled profiling path to a single predictable branch.
// -----------------------------------------------------------------------------
class ReplayProfileScope {
public:
    explicit ReplayProfileScope(ReplayProfilePhase* phase) noexcept
        : phase_(phase) {
        if (phase_ != nullptr) {
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~ReplayProfileScope() {
        if (phase_ != nullptr) {
            const auto end = std::chrono::steady_clock::now();
            phase_->record(std::chrono::duration<double, std::milli>(end - start_).count());
        }
    }

    ReplayProfileScope(const ReplayProfileScope&) = delete;
    ReplayProfileScope& operator=(const ReplayProfileScope&) = delete;

private:
    ReplayProfilePhase* phase_{nullptr};
    std::chrono::steady_clock::time_point start_{};
};

// -----------------------------------------------------------------------------
// ReplayProfile: Deterministic counters plus opt-in phase timings
//
// Counters (instructions, events, blocks, bytes, calls) are deterministic for a
// given run. Timings are host wall-clock observations and are diagnostic only;
// they never feed architectural state or SNDL sequencing.
// -----------------------------------------------------------------------------
struct ReplayProfile {
    bool enabled{false};
    std::string label{"replay"};

    // --- Prepare lifecycle timings ---
    ReplayProfilePhase prepare_total;
    ReplayProfilePhase snapshot_validate;
    ReplayProfilePhase sndl_open;
    ReplayProfilePhase snapshot_inspect;
    ReplayProfilePhase environment_init;
    ReplayProfilePhase snapshot_restore;
    ReplayProfilePhase routing_init;

    // --- Execution lifecycle timings ---
    ReplayProfilePhase run_total;
    ReplayProfilePhase interrupt_injection;
    ReplayProfilePhase cpu_execution;
    ReplayProfilePhase input_routing;
    ReplayProfilePhase checkpoint_total;
    ReplayProfilePhase checkpoint_hash;
    ReplayProfilePhase memory_hash;
    ReplayProfilePhase final_hash;
    ReplayProfilePhase completion_validation;
    ReplayProfilePhase report_generation;

    // --- Cursor internals ---
    ReplayProfilePhase cursor_block_load;
    ReplayProfilePhase cursor_block_crc;
    ReplayProfilePhase cursor_block_decode;

    // --- Deterministic counters ---
    std::uint64_t instructions_executed{0};
    std::uint64_t events_consumed{0};
    std::uint64_t checkpoints_recorded{0};
    std::uint64_t cursor_advances{0};
    std::uint64_t blocks_loaded{0};
    std::uint64_t block_bytes_read{0};
    std::uint64_t records_decoded{0};
    std::uint64_t crc_calls{0};
    std::uint64_t crc_bytes{0};
    std::uint64_t hash_calls{0};
    std::uint64_t memory_hash_calls{0};
    std::uint64_t memory_bytes_hashed{0};
    std::uint64_t input_route_attempts{0};
    std::uint64_t input_routes_consumed{0};

    // --- Artifact / environment facts ---
    std::uint64_t wsnap_file_size{0};
    std::uint64_t sndl_file_size{0};
    std::uint64_t memory_capacity{0};

    // --- Outcome ---
    ReplayCompletionStatus completion_status{ReplayCompletionStatus::CompletedNormally};
    bool diverged{false};

    [[nodiscard]] double accounted_execution_ms() const noexcept {
        return interrupt_injection.total_ms + cpu_execution.total_ms +
               checkpoint_total.total_ms + final_hash.total_ms +
               completion_validation.total_ms;
    }

    [[nodiscard]] double unaccounted_execution_ms() const noexcept {
        const double accounted = accounted_execution_ms();
        return run_total.total_ms > accounted ? run_total.total_ms - accounted : 0.0;
    }
};

// -----------------------------------------------------------------------------
// ReplayProfiler: Human and machine readable profile report generation
// -----------------------------------------------------------------------------
class ReplayProfiler {
public:
    [[nodiscard]] static std::string to_text(const ReplayProfile& profile);
    [[nodiscard]] static std::string to_json(const ReplayProfile& profile);

    [[nodiscard]] static ReplayResult write_text(
        const std::filesystem::path& path,
        const ReplayProfile& profile
    );

    [[nodiscard]] static ReplayResult write_json(
        const std::filesystem::path& path,
        const ReplayProfile& profile
    );
};

} // namespace winios::replay
