#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

namespace winios::graphics {

// -----------------------------------------------------------------------------
// FrameSchedulerStats: Vsync & Timing Diagnostics
// -----------------------------------------------------------------------------
struct FrameSchedulerStats {
    std::uint64_t total_vsyncs{0};
    std::uint64_t rendered_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t missed_deadlines{0};
    double        effective_fps{0.0};
};

// -----------------------------------------------------------------------------
// FrameScheduler: Deterministic Virtual Display Clock & Vsync Scheduler
// -----------------------------------------------------------------------------
class FrameScheduler {
public:
    using VsyncCallback = std::function<void(std::uint64_t frame_seq, std::uint64_t timestamp_us)>;
    using FrameCompleteCallback = std::function<void(std::uint64_t frame_seq, std::uint64_t duration_us)>;

    explicit FrameScheduler(std::uint32_t target_fps = 60u);
    ~FrameScheduler() = default;

    // Configuration
    void set_target_fps(std::uint32_t fps) noexcept;
    [[nodiscard]] std::uint32_t target_fps() const noexcept { return target_fps_; }
    [[nodiscard]] std::uint64_t frame_interval_us() const noexcept { return frame_interval_us_; }

    // Callbacks
    void set_vsync_callback(VsyncCallback cb) { vsync_cb_ = std::move(cb); }
    void set_frame_complete_callback(FrameCompleteCallback cb) { complete_cb_ = std::move(cb); }

    // Deterministic step simulation (advances virtual clock by delta_us)
    // Returns true if one or more vsync boundaries were crossed
    bool advance_time(std::uint64_t delta_us);

    // Explicit Vsync trigger
    void trigger_vsync();

    // Frame lifecycle notifications
    void notify_frame_begin();
    void notify_frame_rendered();
    void notify_frame_dropped();

    // Diagnostics
    [[nodiscard]] std::uint64_t current_frame_sequence() const noexcept { return current_frame_seq_; }
    [[nodiscard]] std::uint64_t virtual_clock_us() const noexcept { return virtual_clock_us_; }
    [[nodiscard]] const FrameSchedulerStats& stats() const noexcept { return stats_; }

private:
    std::uint32_t         target_fps_{60u};
    std::uint64_t         frame_interval_us_{16666u};
    std::uint64_t         virtual_clock_us_{0};
    std::uint64_t         last_vsync_us_{0};
    std::uint64_t         current_frame_seq_{0};
    std::uint64_t         frame_begin_us_{0};

    FrameSchedulerStats   stats_{};
    mutable std::mutex    lock_;
    VsyncCallback         vsync_cb_;
    FrameCompleteCallback complete_cb_;
};

} // namespace winios::graphics
