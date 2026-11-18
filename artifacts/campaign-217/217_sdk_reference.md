# WINIOS CAMPAIGN 217 — DEVELOPER SDK v1.2 REFERENCE

**Header:** `include/winios_sdk/winios_sdk.h`  
**ABI Version:** `0x00010200` (ABI v1.2)  
**Backward Compatibility:** Fully compatible with ABI v1.1 (`0x00010100`)

---

## 1. System Call Numbers

In addition to legacy Darwin/Category C syscalls (500–512), ABI v1.2 introduces syscall numbers 513–521:

| Syscall # | Macro Constant | Purpose |
|---|---|---|
| 513 | `SYS_winios_alloc_heap` | Allocate memory within sandboxed process heap |
| 514 | `SYS_winios_free_heap` | Free memory previously allocated in heap |
| 515 | `SYS_winios_get_runtime_info` | Query allocator and runtime performance counters |
| 516 | `SYS_winios_storage_read` | Read sandboxed isolated storage file |
| 517 | `SYS_winios_storage_write` | Write sandboxed isolated storage file |
| 518 | `SYS_winios_ipc_create_port` | Create named IPC port for message reception |
| 519 | `SYS_winios_ipc_send` | Send message to target IPC port |
| 520 | `SYS_winios_ipc_recv` | Receive message from owned IPC port |
| 521 | `SYS_winios_pref_get` / `SYS_winios_pref_set` | Get or set persistent application preference |

---

## 2. ABI v1.2 Data Structures

### `winios_runtime_info_t` (64 bytes)
```c
typedef struct {
    uint64_t heap_base;
    uint64_t heap_size;
    uint64_t bytes_allocated;
    uint64_t peak_bytes;
    uint32_t active_allocations;
    uint32_t total_allocations;
    uint32_t total_frees;
    uint32_t rejected_allocations;
    uint64_t retired_instructions;
    uint32_t reserved[4];
} winios_runtime_info_t;
```

### `winios_storage_req_t` (64 bytes)
```c
typedef struct {
    uint32_t op;          // 0 = read, 1 = write, 2 = delete, 3 = stat
    uint32_t offset;
    uint64_t guest_buf;
    uint32_t buf_size;
    uint32_t bytes_transferred;
    char     path[40];
} winios_storage_req_t;
```

### `winios_ipc_msg_desc_t` (64 bytes)
```c
typedef struct {
    uint32_t port_id;
    uint32_t sender_pid;
    uint32_t message_id;
    uint32_t payload_len;
    uint64_t payload_buf;
    uint32_t flags;
    uint32_t reserved[9];
} winios_ipc_msg_desc_t;
```

### `winios_pref_entry_t` (64 bytes)
```c
typedef struct {
    char     key[32];
    uint32_t val_type;   // 0 = string, 1 = int, 2 = bool, 3 = float
    uint32_t val_len;
    union {
        int64_t  i_val;
        double   f_val;
        uint32_t b_val;
        char     s_val[24];
    } value;
} winios_pref_entry_t;
```

All structs are enforced at compile time via `static_assert(sizeof(...) == 64)`.
