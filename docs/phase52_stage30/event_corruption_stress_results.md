# SNDL v2 Event Corruption Stress & Failure Recovery

## 1. Adversarial Test Vectors
To ensure robust failure modes, five adversarial corruption patterns were injected into valid SNDL v2 streams:

1. **Corrupted Magic**: The file header magic bytes `WSNDL2` were replaced with `CORRUPT`.
   - Preparation Result: REJECTED
   - Reported Code: `SmpReplayStatus::StreamCorrupt`
2. **Aborted Session Status**: The file footer completion status was mutated from `Completed` (1) to `Aborted` (2).
   - Preparation Result: REJECTED
   - Reported Code: `SmpReplayStatus::StreamCorrupt`
   - Diagnostic Message: Declared session was incomplete.
3. **Truncated Payload**: A valid block payload was truncated mid-record by 37 bytes.
   - Preparation Result: REJECTED
   - Reported Code: `SmpReplayStatus::StreamCorrupt`
4. **Trailing Garbage**: 1,024 bytes of arbitrary entropy were appended after the valid footer.
   - Preparation Result: REJECTED
   - Reported Code: `SmpReplayStatus::StreamCorrupt`
5. **Partial Replay Recovery Semantics**:
   - Tested whether the engine allows unverified or partial replays on corrupted streams.
   - Result: Strictly rejected. WinIOS refuses to execute a partial replay when stream integrity is breached, preventing undefined guest state divergence.
