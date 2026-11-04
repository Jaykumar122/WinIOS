#pragma once

#include "trace_config.h"
#include "trace_event.h"
#include "trace_file_writer.h"
#include "trace_ring_buffer.h"
#include "trace_serializer.h"
#include "trace_types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace winios::trace {

// -----------------------------------------------------------------------------
// TraceManagerState: Explicit lifecycle states
// -----------------------------------------------------------------------------
enum class TraceManagerState : std::uint32_t {
    Unconfigured = 0,
    Configured   = 1,
    Running      = 2,
    Stopping     = 3,
    Stopped      = 4,
    Failed       = 5
};

constexpr inline std::string_view to_string(TraceManagerState st) noexcept {
    switch (st) {
        case TraceManagerState::Unconfigured: return "Unconfigured";
        case TraceManagerState::Configured:   return "Configured";
        case TraceManagerState::Running:      return "Running";
        case TraceManagerState::Stopping:     return "Stopping";
        case TraceManagerState::Stopped:      return "Stopped";
        case TraceManagerState::Failed:       return "Failed";
        default:                              return "UnknownState";
    }
}

// -----------------------------------------------------------------------------
// TraceStatistics: Comprehensive snapshot of manager counters and ring state
// -----------------------------------------------------------------------------
struct TraceStatistics {
    std::uint64_t events_submitted{0};
    std::uint64_t events_accepted{0};
    std::uint64_t events_filtered{0};
    std::uint64_t events_rejected{0};
    std::uint64_t events_dropped{0};
    std::uint64_t events_persisted{0};
    std::uint64_t writer_failures{0};
    std::uint64_t consumer_failures{0};
    std::size_t   ring_high_water_mark{0};
    std::size_t   ring_current_size{0};
    std::size_t   ring_capacity{0};
};

// -----------------------------------------------------------------------------
// TraceManager: Centralized host-side trace coordinator
// -----------------------------------------------------------------------------
class TraceManager {
public:
    TraceManager();
    ~TraceManager();

    // Non-copyable, non-movable to ensure safe concurrency and thread ownership
    TraceManager(const TraceManager&) = delete;
    TraceManager& operator=(const TraceManager&) = delete;
    TraceManager(TraceManager&&) = delete;
    TraceManager& operator=(TraceManager&&) = delete;

    // --- Lifecycle Management ---
    // Configures the manager with the supplied settings.
    // Allowed from: Unconfigured, Configured, Stopped.
    // Returns false if manager is Running, Stopping, or if config is invalid.
    bool configure(const TraceConfig& config);

    // Starts the trace manager.
    // Allowed from: Configured, Stopped.
    // Returns false if Unconfigured, already Running, Stopping, or Failed.
    bool start();

    // Single-producer submission entry point.
    // Accepts events only when state == Running.
    // Applies event-type and core-id filtering before queueing.
    // Pushes into in-memory ring buffer using drop-newest overflow policy.
    bool submit(const TraceEvent& event) noexcept;

    // Flushes pending events from the ring buffer and flushes writer output.
    bool flush();

    // Gracefully stops tracing, drains all pending events to file (if enabled),
    // and finalizes output with a valid file footer.
    // Idempotent: safe to call repeatedly.
    bool stop();

    // --- State and Metric Queries ---
    [[nodiscard]] TraceManagerState state() const noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;

    [[nodiscard]] const TraceConfig& config() const noexcept { return config_; }
    [[nodiscard]] TraceStatistics statistics() const noexcept;

    [[nodiscard]] TraceFileError last_writer_error() const noexcept { return last_writer_error_; }
    [[nodiscard]] TraceConfigError last_config_error() const noexcept { return last_config_error_; }

    // Fast hot-path check to avoid event construction if tracing is inactive or filtered
    [[nodiscard]] bool should_trace(TraceEventType type, std::uint16_t core_id = 0) const noexcept {
        if (state_.load(std::memory_order_relaxed) != TraceManagerState::Running) {
            return false;
        }
        if (core_id < 64 && ((config_.core_mask & (1ULL << core_id)) == 0)) {
            return false;
        }
        return config_.event_types.test(type);
    }

    // Direct ring buffer access for testing and inspection (read-only)
    [[nodiscard]] const InMemoryRingBuffer* ring_buffer() const noexcept { return ring_buffer_.get(); }

private:
    void consumer_loop();
    void drain_remaining_events();

    mutable std::mutex control_mutex_{};
    std::condition_variable cv_{};

    std::atomic<TraceManagerState> state_{TraceManagerState::Unconfigured};
    std::atomic<bool> quit_{false};
    std::atomic<bool> flush_requested_{false};
    std::condition_variable flush_done_cv_{};

    TraceConfig config_{};
    TraceConfigError last_config_error_{TraceConfigError::Success};
    TraceFileError last_writer_error_{TraceFileError::Success};

    std::unique_ptr<InMemoryRingBuffer> ring_buffer_{nullptr};
    TraceFileWriter writer_{};
    std::thread consumer_thread_{};

    // Atomic accounting counters
    std::atomic<std::uint64_t> events_submitted_{0};
    std::atomic<std::uint64_t> events_accepted_{0};
    std::atomic<std::uint64_t> events_filtered_{0};
    std::atomic<std::uint64_t> events_rejected_{0};
    std::atomic<std::uint64_t> events_dropped_{0};
    std::atomic<std::uint64_t> events_persisted_{0};
    std::atomic<std::uint64_t> writer_failures_{0};
    std::atomic<std::uint64_t> consumer_failures_{0};
};

} // namespace winios::trace
