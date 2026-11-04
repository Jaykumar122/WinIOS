#pragma once

#include "trace_event.h"
#include "trace_types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace winios::trace {

// -----------------------------------------------------------------------------
// Constants and Defaults
// -----------------------------------------------------------------------------
constexpr std::size_t MIN_RING_BUFFER_CAPACITY = 2;
constexpr std::size_t DEFAULT_RING_BUFFER_CAPACITY = 262144; // 256K events = 16 MiB
constexpr std::size_t MAX_RING_BUFFER_CAPACITY = 16777216;   // 16M events = 1 GiB
constexpr std::size_t CACHE_LINE_SIZE = 64;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to alignment specifier
#endif

// -----------------------------------------------------------------------------
// InMemoryRingBuffer: Bounded, Lock-Free Single-Producer Single-Consumer (SPSC)
// Ring Buffer for 64-byte TraceEvent records.
// -----------------------------------------------------------------------------
class InMemoryRingBuffer {
public:
    // Constructs ring buffer with requested capacity (rounded up to power of 2).
    // Throws std::invalid_argument if requested_capacity == 0 or > MAX_RING_BUFFER_CAPACITY.
    explicit InMemoryRingBuffer(std::size_t requested_capacity = DEFAULT_RING_BUFFER_CAPACITY);
    ~InMemoryRingBuffer() = default;

    // Non-copyable, non-movable to ensure safe concurrency and storage ownership.
    InMemoryRingBuffer(const InMemoryRingBuffer&) = delete;
    InMemoryRingBuffer& operator=(const InMemoryRingBuffer&) = delete;
    InMemoryRingBuffer(InMemoryRingBuffer&&) = delete;
    InMemoryRingBuffer& operator=(InMemoryRingBuffer&&) = delete;

    // --- Producer API (Must be called by exactly ONE producer thread) ---
    // Attempts to push an event. If the buffer is full, returns false and increments dropped_events.
    bool try_push(const TraceEvent& event) noexcept;

    // Attempts to push up to count events. Returns actual number of events pushed.
    std::size_t push_batch(const TraceEvent* events, std::size_t count) noexcept;

    // --- Consumer API (Must be called by exactly ONE consumer thread) ---
    // Attempts to pop an event into out_event. If buffer is empty, returns false.
    bool try_pop(TraceEvent& out_event) noexcept;

    // Attempts to pop up to max_count events. Returns actual number of events popped.
    std::size_t pop_batch(TraceEvent* out_events, std::size_t max_count) noexcept;

    // --- Inspection and Capacity Queries (Thread-safe, observable at any time) ---
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t requested_capacity() const noexcept { return requested_capacity_; }
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t available() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;

    // --- Statistics ---
    [[nodiscard]] std::uint64_t dropped_events() const noexcept {
        return shared_.dropped_events.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t total_accepted() const noexcept {
        return producer_.total_accepted.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t total_consumed() const noexcept {
        return consumer_.total_consumed.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t high_water_mark() const noexcept {
        return shared_.high_water_mark.load(std::memory_order_relaxed);
    }

    // --- Lifecycle and Reset ---
    // Resets indices and statistics.
    // PRECONDITION: Producer and consumer threads MUST be quiescent (not concurrently calling push/pop).
    void reset() noexcept;

private:
    static std::size_t compute_effective_capacity(std::size_t requested);

    const std::size_t requested_capacity_;
    const std::size_t capacity_;
    const std::size_t mask_;

    // Preallocated event storage (contiguous, cacheline-aligned)
    std::vector<TraceEvent> storage_;

    // Producer state placed on its own 64-byte cacheline to prevent false sharing
    struct alignas(CACHE_LINE_SIZE) ProducerState {
        std::atomic<std::size_t> write_index{0};
        std::size_t cached_read_index{0};
        std::atomic<std::uint64_t> total_accepted{0};
        std::uint8_t padding[40]{};
    } producer_;

    // Consumer state placed on its own 64-byte cacheline to prevent false sharing
    struct alignas(CACHE_LINE_SIZE) ConsumerState {
        std::atomic<std::size_t> read_index{0};
        std::size_t cached_write_index{0};
        std::atomic<std::uint64_t> total_consumed{0};
        std::uint8_t padding[40]{};
    } consumer_;

    // Shared statistics placed on its own 64-byte cacheline
    struct alignas(CACHE_LINE_SIZE) SharedState {
        std::atomic<std::uint64_t> dropped_events{0};
        std::atomic<std::size_t> high_water_mark{0};
        std::uint8_t padding[48]{};
    } shared_;

    void update_high_water_mark(std::size_t current_size) noexcept;
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace winios::trace
