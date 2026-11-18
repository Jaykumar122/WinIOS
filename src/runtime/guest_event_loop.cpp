#include "runtime/guest_event_loop.h"
#include <chrono>

namespace winios::runtime {

GuestEventLoop::GuestEventLoop(uint32_t process_id, size_t max_capacity)
    : process_id_(process_id), max_capacity_(max_capacity) {}

GuestEventLoop::~GuestEventLoop() {
    Clear();
}

bool GuestEventLoop::PostEvent(const winios_event_t& ev, EventSourceProvenance prov) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= max_capacity_) {
        dropped_events_count_++;
        return false; // Queue overflow backpressure
    }
    queue_.push(ProvenancedEvent{ev, prov});
    cv_.notify_one();
    return true;
}

bool GuestEventLoop::PollEvent(winios_event_t* ev_out, EventSourceProvenance* prov_out) {
    if (!ev_out) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) return false;

    auto item = queue_.front();
    queue_.pop();
    *ev_out = item.event;
    if (prov_out) *prov_out = item.provenance;
    return true;
}

bool GuestEventLoop::WaitEvent(winios_event_t* ev_out, uint32_t timeout_ms, EventSourceProvenance* prov_out) {
    if (!ev_out) return false;
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.empty()) {
        if (timeout_ms == 0) return false;
        if (exit_requested_) return false;

        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
            return !queue_.empty() || exit_requested_;
        });
    }

    if (queue_.empty()) return false;
    auto item = queue_.front();
    queue_.pop();
    *ev_out = item.event;
    if (prov_out) *prov_out = item.provenance;
    return true;
}

void GuestEventLoop::RequestExit(int exit_code) {
    std::lock_guard<std::mutex> lock(mutex_);
    exit_requested_ = true;
    exit_code_ = exit_code;
    cv_.notify_all();
}

size_t GuestEventLoop::GetPendingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void GuestEventLoop::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) queue_.pop();
}

} // namespace winios::runtime
