#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>

#include "runtime/ui/guest_ui_framework.h"

namespace winios::runtime::ui {

enum class LayoutDirection : uint32_t {
    kColumn = 0,
    kRow = 1
};

enum class LayoutAlignment : uint32_t {
    kStart = 0,
    kCenter = 1,
    kEnd = 2
};

struct LayoutConstraints {
    uint32_t min_width{0};
    uint32_t max_width{4096};
    uint32_t min_height{0};
    uint32_t max_height{4096};
    uint32_t padding_left{4};
    uint32_t padding_top{4};
    uint32_t padding_right{4};
    uint32_t padding_bottom{4};
    uint32_t spacing{4};
};

class GuestLayoutEngine {
public:
    static void ArrangeLinear(GuestContainer& container,
                              LayoutDirection direction,
                              const LayoutConstraints& constraints,
                              LayoutAlignment alignment = LayoutAlignment::kStart);
};

} // namespace winios::runtime::ui
