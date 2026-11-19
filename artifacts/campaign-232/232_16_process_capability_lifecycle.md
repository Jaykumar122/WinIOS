# Stage 232.16 — Process Identity & Capability Lifecycle Hardening Specification & Evidence

**Campaign:** 232  
**Stage:** 232.16  
**Component:** `winios::kernel::IpcCapabilityBroker`, `winios::kernel::ProcessIdentityManager`, `winios::security::CapabilityManager`  
**Files Modified:**
- `src/kernel/ipc_capability_broker.h`
- `tests/unit/stage232_16_process_capability_tests.cpp`  
**Test Executable:** `build/RelWithDebInfo/stage232_16_process_capability_tests.exe`  
**CTest Target:** Test #934 (`stage232_16_process_capability_tests`)

---

## 1. Problem Statement & Audit Finding (SEC-232-04)

During the Stage 232.1 independent security audit, vulnerability **SEC-232-04** was identified in `IpcCapabilityBroker`:
- When anti-replay was active on an IPC capability token, the code verified:
  ```cpp
  if (sequence_num > 0) {
      if (sequence_num <= token.last_sequence_num) {
          return IpcStatus::ReplayDetected;
      }
      token.last_sequence_num = sequence_num;
  }
  ```
- Because sequence checking was conditionally executed only `if (sequence_num > 0)`, an attacker or compromised guest process could deliberately pass `sequence_num = 0` to bypass replay detection entirely, transmitting duplicate or out-of-sequence messages indefinitely.

---

## 2. Hardened Architecture & Implementation

### 2.1 Anti-Replay Mitigation (SEC-232-04)
1. **Strict Sequence Tokens (`require_strict_sequence`):**
   Added `bool require_strict_sequence{false}` to `IpcCapabilityToken`. When enabled, sequence numbers must be strictly positive ($\ge 1$). Any message sent with `sequence_num == 0` is immediately rejected with `IpcStatus::ReplayDetected`.
2. **Zero-Sequence Bypass Prevention:**
   If a token has anti-replay enabled (`require_strict_sequence == true`), or if it has already processed a message with a sequence number (`token.last_sequence_num > 0`), any subsequent transmission with `sequence_num == 0` is strictly rejected:
   ```cpp
   // 2. Anti-replay verification (SEC-232-04 hardened)
   if (token.require_strict_sequence || token.last_sequence_num > 0 || sequence_num > 0) {
       if (sequence_num == 0 || sequence_num <= token.last_sequence_num) {
           return IpcStatus::ReplayDetected;
       }
       token.last_sequence_num = sequence_num;
   }
   ```
   An attacker can no longer reset or evade the sequence boundary by passing zero.

### 2.2 Generational Process Isolation
- Every process instance is bound to a 64-bit `ProcessHandle` $(PID, Generation)$.
- When a process terminates, its PID enters quarantine. When the PID slot is eventually reallocated, its generation increments deterministically.
- Old capability tokens reference $(PID, Generation_1)$. When the reallocated process $(PID, Generation_2)$ attempts to invoke the token, `validate_handle_locked` rejects the mismatch (`SenderInvalid` / `PermissionDenied`), preventing PID recycling attacks.

### 2.3 Least-Privilege Capability Propagation
- Spawning child processes grants only basic least-privilege capabilities:
  `StoragePrivate | DisplayWindow | LoggingAccess`.
- Parent elevated capabilities (`TaskManage`, `SettingsWrite`, `ProcessSpawn`) are never inherited automatically.
- Upon process termination, `revoke_all(pid)` immediately zeroes all capability masks.

---

## 3. Verification Log Output

```text
========================================================
STAGE 232.16: Process Identity and Capability Lifecycle Tests
========================================================
[Stage 232.16] Testing strict anti-replay rejection on sequence_num == 0 (SEC-232-04)...
[Stage 232.16] Testing non-strict token zero-sequence bypass mitigation...
[Stage 232.16] Testing generational process handle stale rejection...
[Stage 232.16] Testing capability least-privilege propagation and revocation...
All Stage 232.16 tests passed successfully!
```

CTest Result:
```text
      Start 934: stage232_16_process_capability_tests
10/10 Test #934: stage232_16_process_capability_tests ........   Passed    0.01 sec
```
Status: **PASSED (100%)**
