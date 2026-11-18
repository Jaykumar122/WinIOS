# WinIOS Campaign 223 — Layout Engine v1 & Responsive Resizing (Stage 223.4 & 223.5)

## 1. Executive Summary

Stages 223.4 and 223.5 establish **Layout Engine v1** (`winios_layout_node_t`) in the Guest SDK. The layout engine provides flexbox-like responsive layouts (Row, Column, Absolute) with zero dynamic memory allocation.

---

## 2. Layout Hierarchy & Algorithms

```c
typedef enum {
    WINIOS_LAYOUT_COLUMN   = 0, // Vertical stacking
    WINIOS_LAYOUT_ROW      = 1, // Horizontal arrangement
    WINIOS_LAYOUT_ABSOLUTE = 2  // Explicit positioning
} winios_layout_type_t;

typedef enum {
    WINIOS_ALIGN_START   = 0,
    WINIOS_ALIGN_CENTER  = 1,
    WINIOS_ALIGN_END     = 2,
    WINIOS_ALIGN_STRETCH = 3
} winios_align_t;

typedef struct {
    int32_t top, bottom, left, right;
} winios_insets_t;

struct winios_layout_node_s {
    winios_layout_type_t  type;
    winios_rect_t         bounds;
    winios_insets_t       margin;
    winios_insets_t       padding;
    int32_t               spacing;
    uint32_t              flex_weight;
    uint32_t              min_width;
    uint32_t              min_height;
    winios_align_t        align_items;
    winios_layout_node_t* children[WINIOS_LAYOUT_MAX_CHILDREN];
    uint32_t              child_count;
};
```

### Zero-Dynamic-Allocation Contract
In a freestanding ARM64 Mach-O execution environment without guaranteed libc malloc, `winios_layout_node_t` is statically bounded to `WINIOS_LAYOUT_MAX_CHILDREN` (16 children per container). All calculations use deterministic integer division with remainder tracking.

### Resizing & Dynamic Reflow
When container bounds change (e.g., window resize event):
1. Inner available space is computed: `inner_w = bounds.width - padding.left - padding.right`.
2. Fixed child sizes and margins are summed along the main axis.
3. Remaining available space is distributed proportionally according to each child's `flex_weight`.
4. Cross-axis alignment (`WINIOS_ALIGN_CENTER`, `END`, `STRETCH`) positions elements within the container.

---

## 3. Test Verification

`tests/unit/stage223_4_layout_engine_tests.cpp`:
- **Test 1**: Multi-element column layout with fixed header, flex body, and fixed footer (100% PASS).
- **Test 2**: Row layout with varying flex ratios (flex:1 vs flex:2) and responsive reflow across window resizing from 400px to 600px width (100% PASS).
