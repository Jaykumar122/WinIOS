#pragma once

#include <vector>
#include <memory>
#include "runtime/ui/view.h"

namespace winios::runtime::ui {

class ViewGroup : public View {
public:
    explicit ViewGroup(std::uint32_t view_id = 0, std::uint32_t pid = 0);
    ~ViewGroup() override;

    bool add_child(std::shared_ptr<View> child);
    bool remove_child(std::uint32_t view_id);
    void clear_children();

    [[nodiscard]] std::size_t child_count() const noexcept { return children_.size(); }
    [[nodiscard]] std::shared_ptr<View> child_at(std::size_t index) const;

    [[nodiscard]] View* find_view_by_id(std::uint32_t id);
    [[nodiscard]] View* find_view_by_tag(const std::string& tag);

    [[nodiscard]] bool clip_children() const noexcept { return clip_children_; }
    void set_clip_children(bool clip) noexcept { clip_children_ = clip; }

    View* hit_test(std::int32_t px, std::int32_t py) override;
    void draw(RenderContext& ctx) override;

    const std::vector<std::shared_ptr<View>>& children() const noexcept { return children_; }

protected:
    std::vector<std::shared_ptr<View>> children_;
    bool clip_children_{true};
};

} // namespace winios::runtime::ui
