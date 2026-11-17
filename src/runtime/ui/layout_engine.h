#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include "runtime/ui/view_group.h"

namespace winios::runtime::ui {

class VBox : public ViewGroup {
public:
    explicit VBox(std::int32_t spacing = 8, std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] std::int32_t spacing() const noexcept { return spacing_; }
    void set_spacing(std::int32_t s) noexcept { spacing_ = s; request_layout(); }

    void layout();

    void request_layout() override;

private:
    std::int32_t spacing_{8};
};

class HBox : public ViewGroup {
public:
    explicit HBox(std::int32_t spacing = 8, std::uint32_t view_id = 0, std::uint32_t pid = 0);

    [[nodiscard]] std::int32_t spacing() const noexcept { return spacing_; }
    void set_spacing(std::int32_t s) noexcept { spacing_ = s; request_layout(); }

    void layout();

    void request_layout() override;

private:
    std::int32_t spacing_{8};
};

class LayoutEngine {
public:
    static void layout_hierarchy(View& root, const Rect& root_bounds);
};

} // namespace winios::runtime::ui
