#ifndef WINIOS_GUEST_SDK_IPC_H
#define WINIOS_GUEST_SDK_IPC_H

#include "winios/winios_types.h"
#include "winios/system/syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 8)
typedef struct {
    uint32_t sender_pid;
    uint32_t target_port;
    uint64_t data_ptr;
    uint32_t data_size;
    uint32_t message_id;
} winios_ipc_msg_desc_t;

typedef struct {
    uint32_t msg_size;
    uint32_t msg_id;
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t flags;
    uint32_t reserved;
} winios_msg_header_t;
#pragma pack(pop)

uint32_t winios_ipc_port_create(void);
int32_t  winios_ipc_send(uint32_t target_port, uint32_t message_id, const void* data, size_t size);
int32_t  winios_ipc_recv(uint32_t port, void* buf, size_t buf_size, uint32_t* out_sender_pid, uint32_t* out_msg_id);

#ifdef __cplusplus
}
#endif

#endif // WINIOS_GUEST_SDK_IPC_H