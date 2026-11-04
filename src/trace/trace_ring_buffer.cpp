#include "trace_ring_buffer.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace winios::trace {

std::size_t InMemoryRingBuffer::compute_effective_capacity(std::size_t requested) {
    if (requested == 0) {
        throw std::invalid_argument("InMemoryRingBuffer capacity must be non-zero");
    }
    if (requested > MAX_RING_BUFFER_CAPACITY) {
        throw std::invalid_argument("InMemoryRingBuffer capacity exceeds MAX_RING_BUFFER_CAPACITY (16M events)");
    }
    if (requested < MIN_RING_BUFFER_CAPACITY) {
        requested = MIN_RING_BUFFER_CAPACITY;
    }

    // Round up to next power of two
    if ((requested & (requested - 1)) == 0) {
        return requested;
    }

    std::size_t power = 1;
    while (power < requested) {
        power <<= 1;
    }
    return power;
}

InMemoryRingBuffer::InMemoryRingBuffer(std::size_t requested_capacity)
    : requested_capacity_(requested_capacity),
      capacity_(compute_effective_capacity(requested_capacity)),
      mask_(capacity_ - 1),
      storage_(capacity_) {
}

bool InMemoryRingBuffer::try_push(const TraceEvent& event) noexcept {
    const std::size_t write_idx = producer_.write_index.load(std::memory_order_relaxed);

    // Fast-path: Check against cached read index
    if (write_idx - producer_.cached_read_index >= capacity_) {
        // Slow-path: Refresh cached read index with acquire fence
        producer_.cached_read_index = consumer_.read_index.load(std::memory_order_acquire);
        if (write_idx - producer_.cached_read_index >= capacity_) {
            // Buffer is full: Drop event, increment dropped counter, and reject
            shared_.dropped_events.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }

    // Write event into allocated slot
    storage_[write_idx & mask_] = event;

    // Publish completed write to consumer
    producer_.write_index.store(write_idx + 1, std::memory_order_release);

    // Update producer statistics
    producer_.total_accepted.fetch_add(1, std::memory_order_relaxed);
    const std::size_t cur_size = write_idx + 1 - producer_.cached_read_index;
    update_high_water_mark(cur_size);

    return true;
}

std::size_t InMemoryRingBuffer::push_batch(const TraceEvent* events, std::size_t count) noexcept {
    if (!events || count == 0) return 0;

    std::size_t pushed = 0;
    while (pushed < count) {
        if (!try_push(events[pushed])) {
            break;
        }
        ++pushed;
    }
    return pushed;
}

bool InMemoryRingBuffer::try_pop(TraceEvent& out_event) noexcept {
    const std::size_t read_idx = consumer_.read_index.load(std::memory_order_relaxed);

    // Fast-path: Check against cached write index
    if (read_idx == consumer_.cached_write_index) {
        // Slow-path: Refresh cached write index with acquire fence
        consumer_.cached_write_index = producer_.write_index.load(std::memory_order_acquire);
        if (read_idx == consumer_.cached_write_index) {
            // Buffer is empty
            return false;
        }
    }

    // Read event from slot
    out_event = storage_[read_idx & mask_];

    // Publish completed read to producer
    consumer_.read_index.store(read_idx + 1, std::memory_order_release);

    // Update consumer statistics
    consumer_.total_consumed.fetch_add(1, std::memory_order_relaxed);

    return true;
}

std::size_t InMemoryRingBuffer::pop_batch(TraceEvent* out_events, std::size_t max_count) noexcept {
    if (!out_events || max_count == 0) return 0;

    std::size_t popped = 0;
    while (popped < max_count) {
        if (!try_pop(out_events[popped])) {
            break;
        }
        ++popped;
    }
    return popped;
}

std::size_t InMemoryRingBuffer::size() const noexcept {
    const std::size_t w = producer_.write_index.load(std::memory_order_acquire);
    const std::size_t r = consumer_.read_index.load(std::memory_order_acquire);
    return (w >= r) ? (w - r) : 0;
}

std::size_t InMemoryRingBuffer::available() const noexcept {
    const std::size_t s = size();
    return (s < capacity_) ? (capacity_ - s) : 0;
}

bool InMemoryRingBuffer::empty() const noexcept {
    const std::size_t w = producer_.write_index.load(std::memory_order_acquire);
    const std::size_t r = consumer_.read_index.load(std::memory_order_acquire);
    return w == r;
}

bool InMemoryRingBuffer::full() const noexcept {
    return size() >= capacity_;
}

void InMemoryRingBuffer::update_high_water_mark(std::size_t current_size) noexcept {
    std::size_t prev_hwm = shared_.high_water_mark.load(std::memory_order_relaxed);
    while (current_size > prev_hwm) {
        if (shared_.high_water_mark.compare_exchange_weak(
                prev_hwm, current_size, std::memory_order_relaxed)) {
            break;
        }
    }
}

void InMemoryRingBuffer::reset() noexcept {
    // Producer state reset
    producer_.write_index.store(0, std::memory_order_relaxed);
    producer_.cached_read_index = 0;
    producer_.total_accepted.store(0, std::memory_order_relaxed);

    // Consumer state reset
    consumer_.read_index.store(0, std::memory_order_relaxed);
    consumer_.cached_write_index = 0;
    consumer_.total_consumed.store(0, std::memory_order_relaxed);

    // Shared state reset
    shared_.dropped_events.store(0, std::memory_order_relaxed);
    shared_.high_water_mark.store(0, std::memory_order_relaxed);

    // Clear underlying buffer elements to zero
    std::fill(storage_.begin(), storage_.end(), TraceEvent{});
}

} // namespace winios::trace
