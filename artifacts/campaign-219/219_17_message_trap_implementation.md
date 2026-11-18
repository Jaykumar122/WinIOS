# WinIOS Campaign 219 — Stage 219.17: Message Trap Implementation & Port Emulation

## 1. Dispatch & Routing Implementation

When an EL0 process invokes `kSysWiniosMsgTrap` (527) or `kSysMachMsg` (31):
1. The dispatcher parses `winios_msg_header_t` from the guest virtual address.
2. Validates boundaries: `sizeof(hdr) <= msg_size <= 4096`.
3. If payload length (`msg_size - sizeof(hdr) > 0`), reads payload bytes from user space into a temporary kernel buffer.
4. If `dst_port != 0`, dispatches the message to `GuestIpcRouter::Get().Send()`, routing it into the destination process's port queue.
5. Receiver retrieves messages via `SYS_winios_ipc_recv` or message trap receive.

## 2. Test Verification

Verified by `stage219_17_msg_trap_tests.exe`:
- Successful sending of message with ID `0x4321` and string payload to target port.
- Receiver verified receipt of exact payload and metadata.
- Rejection of oversized messages (`msg_size = 8192`) with `EINVAL` (22).
- Rejection of undersized messages (`msg_size = 8`) with `EINVAL` (22).
- Rejection of unmapped memory address with `EFAULT` (14).
