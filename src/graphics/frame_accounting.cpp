#include "graphics/frame_accounting.h"

#include <sstream>

namespace winios::graphics {

FrameAccounting& FrameAccounting::instance() noexcept {
    static FrameAccounting s_instance;
    return s_instance;
}

void FrameAccounting::reset() noexcept {
    stats_ = FrameAccountingStats{};
}

std::string FrameAccounting::format_report() const {
    std::ostringstream ss;
    ss << "=== Frame and Presentation Accounting Report ===\n";
    ss << "  1. Guest Surface Presents:                  " << stats_.guest_surface_presents << "\n";
    ss << "  2. Damage-Triggered Compositor Passes:      " << stats_.damage_triggered_compositor_passes << "\n";
    ss << "  3. Composed Frames:                         " << stats_.composed_frames << "\n";
    ss << "  4. Host Presentation Attempts:              " << stats_.host_presentation_attempts << "\n";
    ss << "  5. Successfully Presented Host Frames:      " << stats_.successful_host_frames << "\n";
    ss << "  6. Repeated Frames (No Pixel Mutation):     " << stats_.repeated_frames << "\n";
    ss << "  7. Dropped Frames:                          " << stats_.dropped_frames << "\n";
    ss << "  8. Vsync / Deadline Events:                 " << stats_.vsync_deadline_events << "\n";
    return ss.str();
}

} // namespace winios::graphics
