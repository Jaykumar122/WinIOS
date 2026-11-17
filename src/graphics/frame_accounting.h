#pragma once

#include <cstdint>
#include <string>

namespace winios::graphics {

struct FrameAccountingStats {
    std::uint64_t guest_surface_presents{0};
    std::uint64_t damage_triggered_compositor_passes{0};
    std::uint64_t composed_frames{0};
    std::uint64_t host_presentation_attempts{0};
    std::uint64_t successful_host_frames{0};
    std::uint64_t repeated_frames{0};
    std::uint64_t dropped_frames{0};
    std::uint64_t vsync_deadline_events{0};
};

class FrameAccounting {
public:
    static FrameAccounting& instance() noexcept;

    void reset() noexcept;

    void record_guest_surface_present() noexcept { ++stats_.guest_surface_presents; }
    void record_damage_compositor_pass() noexcept { ++stats_.damage_triggered_compositor_passes; }
    void record_composed_frame() noexcept { ++stats_.composed_frames; }
    void record_host_presentation_attempt() noexcept { ++stats_.host_presentation_attempts; }
    void record_successful_host_frame() noexcept { ++stats_.successful_host_frames; }
    void record_repeated_frame() noexcept { ++stats_.repeated_frames; }
    void record_dropped_frame() noexcept { ++stats_.dropped_frames; }
    void record_vsync_deadline() noexcept { ++stats_.vsync_deadline_events; }

    [[nodiscard]] const FrameAccountingStats& stats() const noexcept { return stats_; }

    std::string format_report() const;

private:
    FrameAccounting() = default;
    FrameAccountingStats stats_{};
};

} // namespace winios::graphics
