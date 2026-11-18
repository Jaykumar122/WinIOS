#include "renderer/presentation_pacer.h"

#include <chrono>

namespace winios::renderer {

void PresentationPacer::on_frame_presented(std::uint64_t timestamp_ns) {
    if (timestamp_ns == 0) {
        timestamp_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::lock_guard<std::mutex> guard(lock_);
    diag_.total_frames++;

    if (diag_.last_frame_time_ns > 0 && timestamp_ns > diag_.last_frame_time_ns) {
        double delta_ms = static_cast<double>(timestamp_ns - diag_.last_frame_time_ns) / 1'000'000.0;

        if (diag_.average_interval_ms == 0.0) {
            diag_.average_interval_ms = delta_ms;
        } else {
            diag_.average_interval_ms = (diag_.average_interval_ms * 0.9) + (delta_ms * 0.1);
        }

        if (delta_ms > diag_.max_interval_ms) {
            diag_.max_interval_ms = delta_ms;
        }

        if (last_delta_ms_ > 0.0) {
            double cur_jitter = std::abs(delta_ms - last_delta_ms_);
            diag_.jitter_ms = (diag_.jitter_ms * 0.9) + (cur_jitter * 0.1);
        }
        last_delta_ms_ = delta_ms;

        if (diag_.average_interval_ms > 0.0) {
            diag_.fps = 1000.0 / diag_.average_interval_ms;
        }
    }

    diag_.last_frame_time_ns = timestamp_ns;
}

void PresentationPacer::on_frame_dropped() {
    std::lock_guard<std::mutex> guard(lock_);
    diag_.dropped_frames++;
}

void PresentationPacer::reset() {
    std::lock_guard<std::mutex> guard(lock_);
    diag_ = PacingDiagnostics{};
    last_delta_ms_ = 0.0;
}

PacingDiagnostics PresentationPacer::diagnostics() const noexcept {
    std::lock_guard<std::mutex> guard(lock_);
    return diag_;
}

} // namespace winios::renderer
