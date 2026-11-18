#ifndef WINIOS_GUEST_SDK_CONTROL_CENTER_H
#define WINIOS_GUEST_SDK_CONTROL_CENTER_H

#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Category C ABI v2.2 Control Center APIs
 * action: 0 = toggle, 1 = show, 2 = hide, 3 = query
 * returns 1 if visible, 0 if hidden, or negative error code
 */
int winios_control_center_toggle(uint32_t action);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_CONTROL_CENTER_H
