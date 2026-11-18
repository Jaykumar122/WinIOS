#ifndef WINIOS_GUEST_SDK_ACCESSIBILITY_H
#define WINIOS_GUEST_SDK_ACCESSIBILITY_H

#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Category C ABI v2.2 Accessibility APIs
 */

int winios_accessibility_get(winios_accessibility_info_t* out_a11y);
int winios_accessibility_set(uint32_t text_scale, int high_contrast);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_ACCESSIBILITY_H
