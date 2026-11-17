#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "input/virtual_input_device.h"

namespace winios::input {

// -----------------------------------------------------------------------------
// TouchEventQueue: Bounded FIFO Ring Buffer with Overflow Accounting
// -----------------------------------------------------------------------------
class TouchEventQueue {
public:
    static constexpr std::size_t kDefaultCapacity = 256u;

    explicit TouchEventQueue(std::size_t capacity = kDefaultCapacity);
    ~TouchEventQueue() = default;

    TouchEventQueue(const TouchEventQueue&) = delete;
    TouchEventQueue& operator=(const TouchEventQueue&) = delete;

    // Enqueue an event. If full, drops oldest event and increments overflow_count_
    bool push(const InputEvent& event);

    // Dequeue next FIFO event. Returns false if empty
    bool pop(InputEvent& out_event);

    // Look at front of queue without popping
    bool peek(InputEvent& out_event) const;

    // Clear all pending events
    void clear();

    // Queries & Diagnostics
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool full() const noexcept;
    [[nodiscard]] std::uint64_t overflow_count() const noexcept;
    [[nodiscard]] std::uint64_t total_pushed() const noexcept;
    [[nodiscard]] std::uint64_t total_popped() const noexcept;

private:
    std::size_t              capacity_{kDefaultCapacity};
    std::size_t              head_{0};
    std::size_t              tail_{0};
    std::size_t              count_{0};
    std::uint64_t            overflow_count_{0};
    std::uint64_t            total_pushed_{0};
    std::uint64_t            total_popped_{0};
    std::uint64_t            last_timestamp_us_{0};
    mutable std::mutex       lock_;
    std::vector<InputEvent>  buffer_;
};

} // namespace winios::input
