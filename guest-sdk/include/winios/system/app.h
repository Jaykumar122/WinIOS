#ifndef WINIOS_SYSTEM_APP_H
#define WINIOS_SYSTEM_APP_H

#include "../winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Query information for a registered application by bundle identifier
int winios_app_get_info(const char* bundle_id, winios_app_info_t* out_info);

// Enumerate registered applications
int winios_app_get_list(winios_app_list_req_t* req, winios_app_info_t* out_entries);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_SYSTEM_APP_H
