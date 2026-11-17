#include "runtime/ui/layout_engine.h"
#include <algorithm>

namespace winios::runtime::ui {

VBox::VBox(std::int32_t spacing, std::uint32_t view_id, std::uint32_t pid)
    : ViewGroup(view_id, pid), spacing_(spacing) {}

void VBox::request_layout() {
    ViewGroup::request_layout();
    layout();
}

void VBox::layout() {
    std::int32_t cur_y = layout_params_.padding.top;
    std::int32_t content_width = frame_.width - layout_params_.padding.left - layout_params_.padding.right;

    for (auto& child : children_) {
        if (!child || child->visibility() == Visibility::Gone) continue;

        const auto& lp = child->layout_params();
        std::int32_t cw = child->frame().width;
        std::int32_t ch = child->frame().height;

        if (lp.width.mode == DimensionMode::MatchParent) {
            cw = content_width - lp.margin.left - lp.margin.right;
        } else if (lp.width.mode == DimensionMode::Fixed) {
            cw = lp.width.fixed_value;
        }
        cw = std::clamp(cw, lp.min_width, lp.max_width);

        if (lp.height.mode == DimensionMode::Fixed) {
            ch = lp.height.fixed_value;
        }
        ch = std::clamp(ch, lp.min_height, lp.max_height);

        std::int32_t cx = layout_params_.padding.left + lp.margin.left;
        if (lp.horizontal_alignment == Alignment::Center) {
            cx = layout_params_.padding.left + (content_width - cw) / 2;
        } else if (lp.horizontal_alignment == Alignment::End) {
            cx = layout_params_.padding.left + content_width - cw - lp.margin.right;
        }

        cur_y += lp.margin.top;
        child->set_frame({cx, cur_y, cw, ch});
        cur_y += ch + lp.margin.bottom + spacing_;

        // If child is also a ViewGroup, recurse
        auto* nested_vbox = dynamic_cast<VBox*>(child.get());
        if (nested_vbox) nested_vbox->layout();
        auto* nested_hbox = dynamic_cast<HBox*>(child.get());
        if (nested_hbox) nested_hbox->layout();
    }
}

HBox::HBox(std::int32_t spacing, std::uint32_t view_id, std::uint32_t pid)
    : ViewGroup(view_id, pid), spacing_(spacing) {}

void HBox::request_layout() {
    ViewGroup::request_layout();
    layout();
}

void HBox::layout() {
    std::int32_t cur_x = layout_params_.padding.left;
    std::int32_t content_height = frame_.height - layout_params_.padding.top - layout_params_.padding.bottom;

    for (auto& child : children_) {
        if (!child || child->visibility() == Visibility::Gone) continue;

        const auto& lp = child->layout_params();
        std::int32_t cw = child->frame().width;
        std::int32_t ch = child->frame().height;

        if (lp.width.mode == DimensionMode::Fixed) {
            cw = lp.width.fixed_value;
        }
        cw = std::clamp(cw, lp.min_width, lp.max_width);

        if (lp.height.mode == DimensionMode::MatchParent) {
            ch = content_height - lp.margin.top - lp.margin.bottom;
        } else if (lp.height.mode == DimensionMode::Fixed) {
            ch = lp.height.fixed_value;
        }
        ch = std::clamp(ch, lp.min_height, lp.max_height);

        std::int32_t cy = layout_params_.padding.top + lp.margin.top;
        if (lp.vertical_alignment == Alignment::Center) {
            cy = layout_params_.padding.top + (content_height - ch) / 2;
        } else if (lp.vertical_alignment == Alignment::End) {
            cy = layout_params_.padding.top + content_height - ch - lp.margin.bottom;
        }

        cur_x += lp.margin.left;
        child->set_frame({cur_x, cy, cw, ch});
        cur_x += cw + lp.margin.right + spacing_;

        // If child is also a ViewGroup, recurse
        auto* nested_vbox = dynamic_cast<VBox*>(child.get());
        if (nested_vbox) nested_vbox->layout();
        auto* nested_hbox = dynamic_cast<HBox*>(child.get());
        if (nested_hbox) nested_hbox->layout();
    }
}

void LayoutEngine::layout_hierarchy(View& root, const Rect& root_bounds) {
    root.set_frame(root_bounds);
    auto* vb = dynamic_cast<VBox*>(&root);
    if (vb) {
        vb->layout();
    }
    auto* hb = dynamic_cast<HBox*>(&root);
    if (hb) {
        hb->layout();
    }
}

} // namespace winios::runtime::ui
