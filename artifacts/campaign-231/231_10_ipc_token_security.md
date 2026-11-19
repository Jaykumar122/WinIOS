# Stage 231.10 — IPC Capability Token Security

## Overview
Stage 231.10 addresses finding SEC-231-04 by replacing sequential, forgeable IPC capability tokens with 128-bit CSPRNG hex capability tokens, enforcing strict process handle generational binding (`ProcessHandle`), introducing anti-replay sequence checks, and establishing automatic token revocation upon receiver termination or handle recycling.

## Key Implementation Details
1. **128-Bit CSPRNG Capability Tokens**:
   - `IpcCapabilityToken` now contains `token_hex`, a 32-character hexadecimal string representing 128 bits of cryptographic entropy generated via `winios::security::SecureRandom::get_random_hex(16)`.
   - Dual-indexing via `token_id` (non-zero randomized 64-bit ID for backward compatibility) and `token_by_hex_` map.
2. **ProcessHandle Generational Binding**:
   - Both `sender` and `receiver` endpoints are bound to `ProcessHandle` (`pid` + `generation`).
   - If an unauthorized process tries to use a valid capability token, it is rejected with `IpcStatus::SenderInvalid`.
   - If the sender process's generation changes or the sender terminates, sends fail with `IpcStatus::SenderInvalid`.
   - If the receiver process terminates or its PID is re-allocated to a new process instance with a new generation, sends immediately fail with `IpcStatus::StaleProcessHandle` and the token is automatically marked `is_revoked = true`.
3. **Anti-Replay Sequence Tracking**:
   - `send_message` and `send_message_secure` accept an optional `sequence_num` parameter.
   - When `sequence_num > 0`, the broker strictly verifies `sequence_num > token.last_sequence_num`.
   - Out-of-order, stale, or replayed sequence numbers are rejected with `IpcStatus::ReplayDetected`.
4. **Secure Process Token Verification**:
   - `send_message_secure` verifies the sender's process security token against `ProcessIdentityManager::validate_handle_and_token` before routing the message.
5. **Bounded Message Queues and Payloads**:
   - Maximum queue depth per process port: 32 messages (`kMaxPortCapacity`). Excess sends fail with `IpcStatus::QueueFull`.
   - Maximum payload size: 4096 bytes (`kMaxMessageBytes`). Larger payloads fail with `IpcStatus::PayloadTooLarge`.
6. **Cleanup on Termination**:
   - `cleanup_process(pid)` and `cleanup_process(ProcessHandle)` immediately drain message queues and revoke all tokens involving the process.

## Verification
- **Test Suite**: `tests/unit/stage231_10_ipc_token_security_tests.cpp` (CTest #912).
- **Test Results**:
  - RelWithDebInfo: Test #912 Passed in 0.04s.
  - Debug: Test #912 Passed in 0.05s.
  - Regressions: Baseline tests #901 (`stage230_19`) and #904 (`stage230_22`) passed 100%.
