#pragma once

#include <cmath>
#include <cstdint>
#include <mutex>

#include "runtime/interactive_runtime_contract.h"

namespace winios::renderer {

struct PacingDiagnostics {
    std::uint64_t total_frames{0};
    std::uint64_t dropped_frames{0};
    double        fps{0.0};
    double        average_interval_ms{0.0};
    double        max_interval_ms{0.0};
    double        jitter_ms{0.0};
    std::uint64_t last_frame_time_ns{0};
};

class PresentationPacer {
public:
    PresentationPacer() = default;
    ~PresentationPacer() = default;

    void on_frame_presented(std::uint64_t timestamp_ns = 0);
    void on_frame_dropped();
    void reset();

    [[nodiscard]] PacingDiagnostics diagnostics() const noexcept;
    [[nodiscard]] runtime::winios_presentation_stats_t presentation_stats(std::uint32_t refresh_rate_hz = 60) const noexcept;

private:
    mutable std::mutex lock_;
    PacingDiagnostics  diag_{};
    double             last_delta_ms_{0.0};
};

} // namespace winios::renderer
