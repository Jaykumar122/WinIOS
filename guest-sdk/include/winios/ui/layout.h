#ifndef WINIOS_UI_LAYOUT_H
#define WINIOS_UI_LAYOUT_H

#include <stdint.h>
#include <stddef.h>
#include "winios/ui/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// WinIOS Guest Layout Engine v1
// -----------------------------------------------------------------------------

typedef enum {
    WINIOS_LAYOUT_ABSOLUTE = 0,
    WINIOS_LAYOUT_ROW      = 1, // Horizontal sequence
    WINIOS_LAYOUT_COLUMN   = 2  // Vertical sequence
} winios_layout_type_t;

typedef enum {
    WINIOS_ALIGN_START   = 0,
    WINIOS_ALIGN_CENTER  = 1,
    WINIOS_ALIGN_END     = 2,
    WINIOS_ALIGN_STRETCH = 3
} winios_layout_align_t;

typedef struct {
    int32_t top;
    int32_t bottom;
    int32_t left;
    int32_t right;
} winios_insets_t;

#define WINIOS_LAYOUT_MAX_CHILDREN 16

typedef struct winios_layout_node {
    winios_layout_type_t      type;
    winios_layout_align_t     align_items;
    winios_rect_t             bounds;          // Calculated output bounds
    winios_insets_t           margin;
    winios_insets_t           padding;
    int32_t                   spacing;
    uint32_t                  min_width;
    uint32_t                  max_width;
    uint32_t                  min_height;
    uint32_t                  max_height;
    uint32_t                  flex_weight;     // 0 = fixed dimension, >0 = flex share
    uint32_t                  child_count;
    struct winios_layout_node* children[WINIOS_LAYOUT_MAX_CHILDREN];
} winios_layout_node_t;

void winios_layout_init_node(winios_layout_node_t* node, winios_layout_type_t type);
int  winios_layout_add_child(winios_layout_node_t* parent, winios_layout_node_t* child);
void winios_layout_set_padding(winios_layout_node_t* node, int32_t top, int32_t bottom, int32_t left, int32_t right);
void winios_layout_set_margin(winios_layout_node_t* node, int32_t top, int32_t bottom, int32_t left, int32_t right);
void winios_layout_calculate(winios_layout_node_t* root, winios_rect_t container_bounds);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_UI_LAYOUT_H
