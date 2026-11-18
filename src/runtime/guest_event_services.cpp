#include "runtime/guest_event_services.h"

#include <algorithm>
#include "input/input_router.h"
#include "input/virtual_input_device.h"

namespace winios::runtime {

GuestEventServices::GuestEventServices(std::shared_ptr<input::InputRouter> router)
    : router_(std::move(router)) {
}

std::uint32_t GuestEventServices::create_timer(std::uint32_t pid, std::uint32_t interval_ms, bool is_periodic) {
    if (interval_ms == 0) return 0;

    std::lock_guard<std::mutex> guard(lock_);
    // Check per-process quota
    std::size_t proc_timers = 0;
    for (const auto& [_, t] : timers_) {
        if (t.owner_pid == pid && t.active) {
            proc_timers++;
        }
    }
    if (proc_timers >= kMaxTimersPerProcess) {
        return 0; // Quota exceeded
    }

    std::uint32_t tid = next_timer_id_++;
    GuestTimer timer{};
    timer.id = tid;
    timer.owner_pid = pid;
    timer.interval_ms = interval_ms;
    timer.is_periodic = is_periodic;
    timer.next_fire_time_ms = virtual_time_ms_ + interval_ms;
    timer.active = true;

    timers_[tid] = timer;
    return tid;
}

bool GuestEventServices::cancel_timer(std::uint32_t pid, std::uint32_t timer_id) {
    std::lock_guard<std::mutex> guard(lock_);
    auto it = timers_.find(timer_id);
    if (it == timers_.end() || it->second.owner_pid != pid) {
        return false;
    }
    it->second.active = false;
    timers_.erase(it);
    return true;
}

void GuestEventServices::cleanup_process_timers(std::uint32_t pid) {
    std::lock_guard<std::mutex> guard(lock_);
    std::vector<std::uint32_t> to_erase;
    for (const auto& [id, t] : timers_) {
        if (t.owner_pid == pid) {
            to_erase.push_back(id);
        }
    }
    for (std::uint32_t id : to_erase) {
        timers_.erase(id);
    }
}

void GuestEventServices::step(std::uint32_t delta_ms) {
    std::lock_guard<std::mutex> guard(lock_);
    virtual_time_ms_ += delta_ms;

    std::vector<GuestTimer> fired;
    for (auto& [_, t] : timers_) {
        if (t.active && virtual_time_ms_ >= t.next_fire_time_ms) {
            fired.push_back(t);
            if (t.is_periodic) {
                t.next_fire_time_ms = virtual_time_ms_ + t.interval_ms;
            } else {
                t.active = false;
            }
        }
    }

    // Clean up one-shot finished timers
    for (auto it = timers_.begin(); it != timers_.end();) {
        if (!it->second.active) {
            it = timers_.erase(it);
        } else {
            ++it;
        }
    }

    // Deliver events if router available
    if (router_) {
        for (const auto& ft : fired) {
            input::InputEvent ev{};
            ev.type = input::InputEventType::TimerTick;
            ev.target_pid = ft.owner_pid;
            ev.provenance = input::InputProvenance::SyntheticHostEvent;
            ev.key_code = ft.id; // Timer ID
            ev.touch.contact_id = ft.id;
            ev.timestamp_us = virtual_time_ms_ * 1000;
            router_->route_event(ev);
        }
    }
}

std::size_t GuestEventServices::active_timer_count() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return timers_.size();
}

std::size_t GuestEventServices::active_timers_for_process(std::uint32_t pid) const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    std::size_t count = 0;
    for (const auto& [_, t] : timers_) {
        if (t.owner_pid == pid && t.active) count++;
    }
    return count;
}

} // namespace winios::runtime
