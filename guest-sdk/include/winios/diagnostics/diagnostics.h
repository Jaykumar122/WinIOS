#ifndef WINIOS_GUEST_SDK_DIAGNOSTICS_H
#define WINIOS_GUEST_SDK_DIAGNOSTICS_H

#include "winios/winios_types.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t active_processes;
    uint32_t installed_apps;
    uint32_t open_windows;
    uint32_t ipc_ports;
    uint32_t total_crashes;
    uint32_t total_security_checks;
    uint32_t total_security_denials;
    uint32_t reserved;
} winios_platform_diagnostics_t;

int winios_diagnostics_get_summary(winios_platform_diagnostics_t* out_summary);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_DIAGNOSTICS_H
