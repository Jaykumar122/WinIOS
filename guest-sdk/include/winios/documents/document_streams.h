#ifndef WINIOS_GUEST_SDK_DOCUMENT_STREAMS_H
#define WINIOS_GUEST_SDK_DOCUMENT_STREAMS_H

#include "../winios_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stream lifecycle
int winios_doc_stream_open(uint64_t doc_handle, uint32_t mode, uint64_t* out_stream_h);
int winios_doc_stream_read(uint64_t stream_h, void* dst, uint64_t len, uint64_t* out_read);
int winios_doc_stream_write(uint64_t stream_h, const void* src, uint64_t len, uint64_t* out_written);
int winios_doc_stream_seek(uint64_t stream_h, uint64_t offset, uint64_t* out_cursor);
int winios_doc_stream_close(uint64_t stream_h);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_DOCUMENT_STREAMS_H
