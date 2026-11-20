# Campaign 237 — Stage 237.12: IPC Capability & Generational Token Repairs

## 1. Executive Summary

Stage 237.12 investigated and resolved failure #912 (`stage231_10_ipc_token_security_tests`), which verifies capability token revocation, generational process handle validation, and anti-replay protection.

---

## 2. Root Cause & Technical Repair

### 2.1 Stale Handle Auto-Revocation (#912)
- **Root Cause**:
  In `IpcCapabilityBroker::send_message` and `send_message_locked`, when a message was sent to a receiver whose generation or process identity was stale (`!validate_handle_locked(token.receiver)`), the broker returned `IpcStatus::StaleProcessHandle`. However, it failed to set `token.is_revoked = true`.
  According to the Campaign 231 capability architecture, detecting a stale or terminated receiver must immediately and permanently auto-revoke the capability token. Because the token remained un-revoked, subsequent message sends after slot reallocation returned `StaleProcessHandle` again instead of `IpcStatus::PermissionDenied`.

- **Correction**:
  In `src/kernel/ipc_capability_broker.h` (in both `send_message` and `send_message_locked`), set `token.is_revoked = true` upon detecting a stale receiver handle:
  ```cpp
  // Verify receiver handle (check for stale generation)
  if (!validate_handle_locked(token.receiver)) {
      token.is_revoked = true;
      return IpcStatus::StaleProcessHandle;
  }
  ```

---

## 3. Verification

- `stage231_10_ipc_token_security_tests` passed 100% across all 6 capability test suites (rights checking, invalid sender detection, anti-replay sequence verification, generational binding & auto-revocation, process cleanup, and spoofing resistance).
