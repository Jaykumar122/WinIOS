#include "runtime/ui/guest_layout_engine.h"
#include <algorithm>

namespace winios::runtime::ui {

void GuestLayoutEngine::ArrangeLinear(GuestContainer& container,
                                      LayoutDirection direction,
                                      const LayoutConstraints& constraints,
                                      LayoutAlignment alignment) {
    (void)alignment;
    const auto& children = container.GetChildren();
    if (children.empty()) return;

    winios_rect_t parent_frame = container.GetFrame();
    uint32_t current_x = parent_frame.x + constraints.padding_left;
    uint32_t current_y = parent_frame.y + constraints.padding_top;

    for (const auto& child : children) {
        if (!child || !child->IsVisible()) continue;
        winios_rect_t child_frame = child->GetFrame();

        uint32_t child_w = std::clamp(child_frame.width, constraints.min_width, constraints.max_width);
        uint32_t child_h = std::clamp(child_frame.height, constraints.min_height, constraints.max_height);

        child->SetFrame(static_cast<int32_t>(current_x),
                        static_cast<int32_t>(current_y),
                        child_w,
                        child_h);

        if (direction == LayoutDirection::kColumn) {
            current_y += child_h + constraints.spacing;
        } else {
            current_x += child_w + constraints.spacing;
        }
    }
}

} // namespace winios::runtime::ui
