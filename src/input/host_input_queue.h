#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "input/virtual_input_device.h"

namespace winios::input {

enum class QueueDropPolicy : std::uint8_t {
    DropOldest = 0,
    DropNewest = 1
};

struct QueueStats {
    std::size_t count{0};
    std::size_t capacity{0};
    std::uint64_t total_pushed{0};
    std::uint64_t total_popped{0};
    std::uint64_t total_dropped{0};
    std::uint64_t last_timestamp_us{0};
};

class HostInputQueue {
public:
    static constexpr std::size_t kDefaultCapacity = 256u;

    explicit HostInputQueue(std::size_t capacity = kDefaultCapacity,
                            QueueDropPolicy policy = QueueDropPolicy::DropOldest);
    ~HostInputQueue() = default;

    HostInputQueue(const HostInputQueue&) = delete;
    HostInputQueue& operator=(const HostInputQueue&) = delete;

    // Enqueue event according to drop policy
    bool push(const InputEvent& event);

    // Dequeue next FIFO event
    bool pop(InputEvent& out_event);

    // Look at front of queue without popping
    bool peek(InputEvent& out_event) const;

    // Drain up to max_events into output vector
    std::size_t drain(std::vector<InputEvent>& out_events, std::size_t max_events);

    // Clear all pending events
    void clear();

    // Diagnostics & Statistics
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;
    [[nodiscard]] std::uint64_t dropped_count() const noexcept;
    [[nodiscard]] QueueStats stats() const noexcept;

private:
    std::size_t              capacity_{kDefaultCapacity};
    QueueDropPolicy          policy_{QueueDropPolicy::DropOldest};
    std::size_t              head_{0};
    std::size_t              tail_{0};
    std::size_t              count_{0};
    std::uint64_t            total_pushed_{0};
    std::uint64_t            total_popped_{0};
    std::uint64_t            total_dropped_{0};
    std::uint64_t            last_timestamp_us_{0};
    mutable std::mutex       lock_;
    std::vector<InputEvent>  buffer_;
};

} // namespace winios::input
