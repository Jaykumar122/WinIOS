#ifndef WINIOS_GUEST_SDK_DOCUMENTS_H
#define WINIOS_GUEST_SDK_DOCUMENTS_H

#include "../winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Document picker operations
int winios_doc_open_picker(uint64_t caller_win, const char* filter_ext, uint32_t* out_req_id);
int winios_doc_save_picker(uint64_t caller_win, const char* default_name, const char* filter_ext, uint32_t* out_req_id);

// Metadata query
int winios_doc_get_metadata(uint64_t doc_handle, winios_doc_metadata_t* out_meta);

// Access grant
int winios_doc_grant_access(uint64_t doc_handle, uint32_t grantee_pid, uint32_t mode, uint64_t* out_token);

// Recent documents query
int winios_doc_get_recent(winios_recent_doc_t* out_recents, uint32_t max_count, uint32_t* out_count);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_DOCUMENTS_H
