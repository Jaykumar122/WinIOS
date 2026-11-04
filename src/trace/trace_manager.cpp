#include "trace/trace_manager.h"

#include <algorithm>
#include <chrono>

namespace winios::trace {

TraceManager::TraceManager() = default;

TraceManager::~TraceManager() {
    stop();
}

bool TraceManager::configure(const TraceConfig& config) {
    std::lock_guard<std::mutex> lock(control_mutex_);

    const auto current = state_.load(std::memory_order_acquire);
    if (current == TraceManagerState::Running || current == TraceManagerState::Stopping) {
        return false;
    }

    const auto err = config.validate();
    if (err != TraceConfigError::Success) {
        last_config_error_ = err;
        return false;
    }

    config_ = config;
    last_config_error_ = TraceConfigError::Success;

    if (config_.enabled) {
        ring_buffer_ = std::make_unique<InMemoryRingBuffer>(config_.ring_capacity);
    } else {
        ring_buffer_.reset();
    }

    state_.store(TraceManagerState::Configured, std::memory_order_release);
    return true;
}

bool TraceManager::start() {
    std::lock_guard<std::mutex> lock(control_mutex_);

    const auto current = state_.load(std::memory_order_acquire);
    if (current != TraceManagerState::Configured && current != TraceManagerState::Stopped) {
        return false;
    }

    if (!config_.enabled) {
        return false;
    }

    if (config_.file_output_enabled) {
        // Open file writer with overwrite = true
        if (!writer_.open(config_.output_path, true)) {
            last_writer_error_ = writer_.last_error();
            writer_failures_.fetch_add(1, std::memory_order_relaxed);
            state_.store(TraceManagerState::Failed, std::memory_order_release);
            return false;
        }

        quit_.store(false, std::memory_order_release);
        flush_requested_.store(false, std::memory_order_release);
        consumer_thread_ = std::thread(&TraceManager::consumer_loop, this);
    }

    state_.store(TraceManagerState::Running, std::memory_order_release);
    return true;
}

bool TraceManager::submit(const TraceEvent& event) noexcept {
    events_submitted_.fetch_add(1, std::memory_order_relaxed);

    if (state_.load(std::memory_order_acquire) != TraceManagerState::Running) {
        events_rejected_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Core ID filtering
    if (event.core_id < 64) {
        if ((config_.core_mask & (1ULL << event.core_id)) == 0) {
            events_filtered_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    } else if (config_.core_mask != ~std::uint64_t{0}) {
        events_filtered_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Event type filtering
    if (!config_.event_types.test(event.event_type)) {
        events_filtered_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Attempt push to in-memory ring buffer (Single-Producer)
    if (ring_buffer_ && ring_buffer_->try_push(event)) {
        events_accepted_.fetch_add(1, std::memory_order_relaxed);
        if (config_.file_output_enabled) {
            cv_.notify_one();
        }
        return true;
    }

    // Ring buffer overflow (drop-newest)
    events_dropped_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool TraceManager::flush() {
    const auto current = state_.load(std::memory_order_acquire);
    if (current != TraceManagerState::Running && current != TraceManagerState::Stopped) {
        return false;
    }

    if (!config_.file_output_enabled || !consumer_thread_.joinable()) {
        return true;
    }

    flush_requested_.store(true, std::memory_order_release);
    cv_.notify_one();

    std::unique_lock<std::mutex> lock(control_mutex_);
    const bool flushed = flush_done_cv_.wait_for(lock, std::chrono::seconds(2), [this] {
        return !flush_requested_.load(std::memory_order_acquire) ||
               state_.load(std::memory_order_acquire) == TraceManagerState::Failed;
    });

    return flushed && (state_.load(std::memory_order_acquire) != TraceManagerState::Failed);
}

bool TraceManager::stop() {
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        const auto current = state_.load(std::memory_order_acquire);
        if (current == TraceManagerState::Stopped || current == TraceManagerState::Unconfigured) {
            return true;
        }
        state_.store(TraceManagerState::Stopping, std::memory_order_release);
        quit_.store(true, std::memory_order_release);
    }

    cv_.notify_all();

    if (consumer_thread_.joinable()) {
        consumer_thread_.join();
    }

    std::lock_guard<std::mutex> lock(control_mutex_);
    if (state_.load(std::memory_order_acquire) == TraceManagerState::Failed) {
        return false;
    }

    state_.store(TraceManagerState::Stopped, std::memory_order_release);
    return true;
}

void TraceManager::consumer_loop() {
    const std::size_t batch_size = (config_.writer_batch_size > 0) ? config_.writer_batch_size : 1024;
    std::vector<TraceEvent> batch(batch_size);

    while (!quit_.load(std::memory_order_relaxed)) {
        if (!ring_buffer_) {
            break;
        }

        const std::size_t popped = ring_buffer_->pop_batch(batch.data(), batch.size());
        if (popped > 0) {
            if (!writer_.write_events(batch.data(), popped)) {
                writer_failures_.fetch_add(1, std::memory_order_relaxed);
                last_writer_error_ = writer_.last_error();
                state_.store(TraceManagerState::Failed, std::memory_order_release);
                return;
            }
            events_persisted_.fetch_add(popped, std::memory_order_relaxed);
            continue;
        }

        if (flush_requested_.load(std::memory_order_acquire)) {
            if (!writer_.flush()) {
                writer_failures_.fetch_add(1, std::memory_order_relaxed);
                last_writer_error_ = writer_.last_error();
                state_.store(TraceManagerState::Failed, std::memory_order_release);
                return;
            }
            flush_requested_.store(false, std::memory_order_release);
            flush_done_cv_.notify_all();
        }

        std::unique_lock<std::mutex> lock(control_mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
            return quit_.load(std::memory_order_relaxed) ||
                   flush_requested_.load(std::memory_order_relaxed) ||
                   (ring_buffer_ && !ring_buffer_->empty());
        });
    }

    // Drain any remaining events in ring buffer
    drain_remaining_events();

    if (writer_.is_open()) {
        if (!writer_.close()) {
            writer_failures_.fetch_add(1, std::memory_order_relaxed);
            last_writer_error_ = writer_.last_error();
            state_.store(TraceManagerState::Failed, std::memory_order_release);
        }
    }
}

void TraceManager::drain_remaining_events() {
    if (!ring_buffer_ || !writer_.is_open()) {
        return;
    }

    const std::size_t batch_size = (config_.writer_batch_size > 0) ? config_.writer_batch_size : 1024;
    std::vector<TraceEvent> batch(batch_size);

    while (!ring_buffer_->empty()) {
        const std::size_t popped = ring_buffer_->pop_batch(batch.data(), batch.size());
        if (popped == 0) {
            break;
        }
        if (!writer_.write_events(batch.data(), popped)) {
            writer_failures_.fetch_add(1, std::memory_order_relaxed);
            last_writer_error_ = writer_.last_error();
            state_.store(TraceManagerState::Failed, std::memory_order_release);
            return;
        }
        events_persisted_.fetch_add(popped, std::memory_order_relaxed);
    }
}

TraceManagerState TraceManager::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}

bool TraceManager::enabled() const noexcept {
    return config_.enabled;
}

bool TraceManager::is_running() const noexcept {
    return state_.load(std::memory_order_acquire) == TraceManagerState::Running;
}

TraceStatistics TraceManager::statistics() const noexcept {
    TraceStatistics stats{};
    stats.events_submitted = events_submitted_.load(std::memory_order_relaxed);
    stats.events_accepted = events_accepted_.load(std::memory_order_relaxed);
    stats.events_filtered = events_filtered_.load(std::memory_order_relaxed);
    stats.events_rejected = events_rejected_.load(std::memory_order_relaxed);
    stats.events_dropped = events_dropped_.load(std::memory_order_relaxed);
    stats.events_persisted = events_persisted_.load(std::memory_order_relaxed);
    stats.writer_failures = writer_failures_.load(std::memory_order_relaxed);
    stats.consumer_failures = consumer_failures_.load(std::memory_order_relaxed);

    if (ring_buffer_) {
        stats.ring_high_water_mark = ring_buffer_->high_water_mark();
        stats.ring_current_size = ring_buffer_->size();
        stats.ring_capacity = ring_buffer_->capacity();
    }
    return stats;
}

} // namespace winios::trace
