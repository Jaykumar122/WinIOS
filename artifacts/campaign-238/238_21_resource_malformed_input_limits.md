# Stage 238.21: Resource and Malformed Input Limits

## 1. Overview
Stage 238.21 evaluated resource exhaustion safeguards, malformed payload boundaries, and memory consumption limits across kernel syscalls, VFS, and IPC.

## 2. Resource Boundaries Verified
1. **Syscall Argument Limits**:
   - String copy-in operations enforce `kMaxPathLength = 256` and `kMaxStringLength = 1024`.
   - Guest string pointers that do not null-terminate within limit return `ENAMETOOLONG` or `EINVAL`.
2. **IPC Message & Queue Limits**:
   - `kMaxMessageBytes = 4096` per IPC message payload. Larger payloads are rejected with `IpcStatus::PayloadTooLarge`.
   - `kMaxQueueDepth = 64` per process mailbox. Saturated queues reject incoming messages with `IpcStatus::QueueFull`.
3. **Window Count & Surface Memory**:
   - Maximum window count enforced per process to avoid GDI/DirectX handle starvation.
   - Zero or negative dimensions rejected with `EINVAL` before allocation.

## 3. Findings
All boundary limits enforce safe failure without resource exhaustion or memory leaks.
