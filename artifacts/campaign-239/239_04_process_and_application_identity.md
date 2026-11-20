# Stage 239.4: Process and Application Identity

## 1. Identity Hierarchy & Model
In WinIOS, application and execution identity are organized in a strict four-level hierarchy:

```
[ Application Bundle Identity (bundle_id) ]
    e.g. "com.winios.calculator"
                     |
                     v
[ Application Registration (package_version, permissions, min_sdk) ]
    Managed by ApplicationRegistry in VFS /Applications/
                     |
                     v
[ Process Instance (instance_id, start_time, security_token) ]
    Monotonically increasing instantiation counter
                     |
                     v
[ Generational Process Handle (ProcessHandle { pid, generation }) ]
    PID: 100 .. 32768
    Generation: Monotonic uint32 per PID slot
```

## 2. Anti-Recycling and Stale Handle Invariants

1. **PID Slot Generation**:
   - Every PID slot maintains a monotonic generation counter (`slot_generations_[pid]`).
   - When a process terminates, its handle becomes permanently invalid: `is_valid_handle({pid, old_gen}) == false`.
   - When PID slot is re-allocated, `gen = ++slot_generations_[pid]`.
   - Any client holding `{pid, old_gen}` is rejected with `StaleProcessHandle` or `SenderInvalid`.
2. **PID Quarantine Ring Buffer**:
   - Terminated PIDs are placed into a FIFO quarantine queue of depth `kDefaultQuarantineDepth = 16`.
   - A PID is not re-eligible for allocation until at least 16 other processes have cycled, preventing rapid PID reuse.
3. **Generational Wraparound Protection (Slot Retirement)**:
   - If a PID's generation reaches `kMaxGeneration` (`0xFFFFFFFFU`), the PID is permanently retired (`retired_pids_`) and never allocated again.
4. **Security Tokens**:
   - Each active instance receives a cryptographically secure 128-bit CSPRNG hex token (`security_token`).
   - On termination, `security_token` is cleared to prevent token reuse.

## 3. Findings
Process and application identity boundaries are verified and enforced.
