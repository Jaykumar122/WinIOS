# WinIOS Campaign 219 — Stage 219.16: Darwin-Inspired Bounded Message Trap ABI

## 1. Trap Interface Specification

The message trap (`SYS_winios_msg_trap` / `SYS_mach_msg`, 527 & 31) provides structured capability-based IPC between guest processes.

### Message Header Specification (`winios_msg_header_t`, 24 bytes)

```c
typedef struct {
    uint32_t msg_size;     // Offset 0x00: Size of header + payload in bytes
    uint32_t msg_id;       // Offset 0x04: User message ID or function code
    uint32_t src_port;     // Offset 0x08: Sending port capability
    uint32_t dst_port;     // Offset 0x0C: Destination port capability
    uint32_t flags;        // Offset 0x10: Message flags
    uint32_t reserved;     // Offset 0x14: Alignment padding
} winios_msg_header_t;
```

## 2. Boundary Controls

1. **Upper Bound**: Total message size (`msg_size`) cannot exceed 4096 bytes. Requests exceeding 4096 bytes are immediately rejected with `EINVAL` (22).
2. **Lower Bound**: Total message size cannot be less than `sizeof(winios_msg_header_t)` (24 bytes). Smaller sizes are rejected with `EINVAL` (22).
3. **Buffer Validation**: User pointer `msg_va` must reside in valid userspace (`< 32 GiB`) and all memory from `msg_va` to `msg_va + msg_size` must be mapped. Invalid or unmapped pointers trigger `EFAULT` (14).
