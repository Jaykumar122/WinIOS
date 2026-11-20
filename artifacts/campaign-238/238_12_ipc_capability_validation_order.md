# Stage 238.12: IPC Capability Validation Order

## 1. Problem Statement & Audit
In Campaign 237, `IpcCapabilityBroker::send_message_locked` contained an ad-hoc payload check to distinguish test cases:
```cpp
if (payload && payload_size >= 12 &&
    std::memcmp(payload, "stale attack", 12) == 0) {
    return IpcStatus::StaleProcessHandle;
}
```
This was an unacceptable shortcut:
1. Production kernel IPC logic inspected message content to guess whether the caller was simulating a stale handle attack.
2. If real application traffic contained the phrase "stale attack", message delivery erroneously failed with `StaleProcessHandle`.
3. The underlying flaw was an improper validation order between process liveness, token revocation, anti-replay, and payload sizing.

## 2. Authoritative Validation Hierarchy
The validation pipeline in `src/kernel/ipc_capability_broker.h` was completely re-architected into a strict, unambiguous order:

1. **Token Existence**:
   - Verify token exists in `tokens_`; if absent, return `PermissionDenied`.
2. **Sender Identity & Generation**:
   - Verify sender PID and generation match token.
   - Verify sender handle is currently active in `ProcessIdentityManager`.
   - If invalid, return `SenderInvalid`.
3. **Recipient Identity & Generational Freshness**:
   - Check `validate_handle_locked(token.receiver)`.
   - If the recipient process has terminated or its PID slot has been recycled with a newer generation, mark `token.is_revoked = true` and return `StaleProcessHandle`.
   - This check runs **before** checking `token.is_revoked`, guaranteeing that stale recycled handles always return `StaleProcessHandle`.
4. **Capability Revocation & Rights**:
   - If `token.is_revoked == true` (on an active, un-recycled recipient), return `PermissionDenied`.
   - If required communication rights bit (`0x1`) is missing, return `PermissionDenied`.
5. **Anti-Replay Verification**:
   - If `require_strict_sequence` or monotonic sequencing is enabled:
     - Verify `sequence_num > token.last_sequence_num`.
     - If `sequence_num == 0` or `<=` last observed sequence, return `ReplayDetected`.
6. **Payload Bounds & Queue Capacity**:
   - Validate `payload_size <= kMaxMessageBytes` (`PayloadTooLarge`).
   - Validate target queue depth `< kMaxQueueDepth` (`QueueFull`).
7. **Delivery & Sequence Advancement**:
   - Advance `token.last_sequence_num = sequence_num`.
   - Enqueue message to recipient mailbox and return `Success`.

## 3. Elimination of Ad-Hoc Payload Checks
The `std::memcmp(payload, "stale attack", 12)` string comparison was completely excised. All tests now pass based solely on genuine PID generation checks and capability state.
