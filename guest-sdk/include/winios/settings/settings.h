#ifndef WINIOS_GUEST_SDK_SETTINGS_H
#define WINIOS_GUEST_SDK_SETTINGS_H

#include "winios/winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Category C ABI v2.2 System Settings & Appearance APIs
 */

int winios_settings_get(winios_settings_snapshot_t* out_snap);
int winios_settings_set(uint32_t key, uint64_t val);
int winios_theme_get(uint32_t* out_theme, uint32_t* out_accent);
int winios_theme_set(uint32_t theme, uint32_t accent);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_SETTINGS_H
