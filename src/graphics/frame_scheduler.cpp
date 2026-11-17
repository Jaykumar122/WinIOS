#include "graphics/frame_scheduler.h"

#include <algorithm>

namespace winios::graphics {

FrameScheduler::FrameScheduler(std::uint32_t target_fps) {
    set_target_fps(target_fps);
}

void FrameScheduler::set_target_fps(std::uint32_t fps) noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    target_fps_ = (fps == 0) ? 60u : fps;
    frame_interval_us_ = 1000000ULL / target_fps_;
}

bool FrameScheduler::advance_time(std::uint64_t delta_us) {
    std::lock_guard<std::mutex> guard(lock_);
    virtual_clock_us_ += delta_us;

    bool triggered = false;
    while (virtual_clock_us_ >= last_vsync_us_ + frame_interval_us_) {
        last_vsync_us_ += frame_interval_us_;
        current_frame_seq_++;
        stats_.total_vsyncs++;
        triggered = true;

        if (virtual_clock_us_ > last_vsync_us_ + (2 * frame_interval_us_)) {
            stats_.missed_deadlines++;
        }

        if (vsync_cb_) {
            vsync_cb_(current_frame_seq_, last_vsync_us_);
        }
    }

    if (virtual_clock_us_ > 0) {
        stats_.effective_fps = (static_cast<double>(stats_.rendered_frames) * 1000000.0) /
                               static_cast<double>(virtual_clock_us_);
    }

    return triggered;
}

void FrameScheduler::trigger_vsync() {
    std::lock_guard<std::mutex> guard(lock_);
    current_frame_seq_++;
    last_vsync_us_ = virtual_clock_us_;
    stats_.total_vsyncs++;

    if (vsync_cb_) {
        vsync_cb_(current_frame_seq_, virtual_clock_us_);
    }
}

void FrameScheduler::notify_frame_begin() {
    std::lock_guard<std::mutex> guard(lock_);
    frame_begin_us_ = virtual_clock_us_;
}

void FrameScheduler::notify_frame_rendered() {
    std::lock_guard<std::mutex> guard(lock_);
    stats_.rendered_frames++;
    std::uint64_t duration = (virtual_clock_us_ >= frame_begin_us_) ?
                             (virtual_clock_us_ - frame_begin_us_) : 0;

    if (complete_cb_) {
        complete_cb_(current_frame_seq_, duration);
    }
}

void FrameScheduler::notify_frame_dropped() {
    std::lock_guard<std::mutex> guard(lock_);
    stats_.dropped_frames++;
}

} // namespace winios::graphics
