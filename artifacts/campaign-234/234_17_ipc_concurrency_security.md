# Stage 234.17: IPC Capability and Anti-Replay Concurrency Security

## Executive Summary
Stage 234.17 validates the concurrency invariants, anti-replay protections, and process lifecycle bindings of the inter-process communication broker (`IpcCapabilityBroker` in `src/kernel/ipc_capability_broker.h`). Under multithreaded contention, duplicate sequence numbers are serialized and rejected, stale process handles resulting from PID recycling are blocked, generation wraparound is prohibited, and dynamic capability token revocation takes effect atomically.

---

## 1. Concurrency Threats & Mitigations

### 1.1 Concurrent Duplicate Sequence Injection
- **Threat:** Multiple adversary threads or network-forwarded packets attempt to submit duplicate sequence numbers concurrently to replay transactions or double-spend IPC operations.
- **Verification:** 4 concurrent threads attempted to deliver the exact same sequence number ($N = 42$) simultaneously. Internal mutex synchronization ensured that exactly 1 thread succeeded; the remaining 3 threads observed $N \le \text{last\_sequence\_num}$ and were immediately rejected with `IpcStatus::ReplayDetected`.
- Monotonic sequence advancement prevents all subsequent attempts with sequence $\le 42$.

### 1.2 Process Termination & Stale Generation Protection (Anti-PID Recycling)
- **Threat:** Process A communicates with Process B at PID 10. Process B terminates. Later, an unrelated process C is launched by the scheduler in PID slot 10. Process A sends confidential data intended for Process B to PID 10.
- **Verification:**
  - `ProcessHandle` includes both `pid` and `generation`.
  - Process termination marks generation 1 inactive and advances the slot generation to 2.
  - When Process A invokes `send_message` with the token bound to `{pid: 10, generation: 1}`, the broker evaluates `validate_handle_locked` and detects that the active generation in slot 10 is 2.
  - The message is rejected with `IpcStatus::StaleProcessHandle`, preventing cross-application data leakage.

### 1.3 Generational Wraparound Protection
- **Threat:** Long-running system operation generates $2^{32}$ process cycles, wrapping generation numbers around to 1 and re-colliding with ancient stale tokens.
- **Verification:**
  - In `ProcessIdentityManager::allocate_process`:
    ```cpp
    if (gen == 0 || gen >= kMaxGeneration) {
        return ProcessHandle{0, 0}; // Generation exhausted, retired permanently
    }
    ```
  - When slot generation reaches `kMaxGeneration`, the PID slot is permanently retired. The allocator skips the slot or fails, guaranteeing that generations never wrap around.

### 1.4 Real-Time Token Revocation Under Contcurrency
- **Threat:** Concurrent worker threads are transmitting messages while a security service revokes the communication token.
- **Verification:** Multiple sender threads continuously transmitted messages while a revoker thread called `revoke_token()`. Mutex locking ensured an instantaneous atomic cut-off: all transmissions initiated after revocation returned `IpcStatus::PermissionDenied`.

---

## 2. Test Verification
The dedicated suite `tests/unit/stage234_17_ipc_concurrency_tests.cpp` was compiled with MSVC C++20 (`/W4 /WX /permissive-`) and executed:
- `RelWithDebInfo`: Passed 100%
- `Debug`: Passed 100%
