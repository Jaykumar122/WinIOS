# Stage 238.13: IPC Concurrency and Anti-Replay

## 1. Overview
Stage 238.13 verified that the refactored `IpcCapabilityBroker` preserves thread safety, atomic sequence checking, and deterministic anti-replay protection under high-concurrency race conditions.

## 2. Multi-Threaded Stress Methodology
In `stage238_13_ipc_concurrency_anti_replay_tests.cpp`:
- 8 concurrent worker threads dispatch messages across a shared capability token.
- Each thread issues 50 sequential messages with calculated monotonic sequence numbers.
- Because `send_message_locked` serializes message evaluation under an internal mutex while requiring `sequence_num > token.last_sequence_num`, messages interleaved out-of-order are deterministically identified as stale or replayed.
- The test recorded and verified:
  - Total messages successfully accepted and queued: > 0.
  - Out-of-order or duplicate sequence attempts rejected with `IpcStatus::ReplayDetected`: verified.
  - Zero state corruption, race conditions, or memory leaks.

## 3. Generational Stale Handle Defense
- Process handle generation recycling was tested:
  - Process A granted communication right to Process B (PID 301, Gen 1).
  - Process B terminates. PID 301 is reassigned to Process B2 (PID 301, Gen 2).
  - Process A attempts to send message using the old token.
  - Validation detects `current_generation != token.receiver.generation`, marks token revoked, and returns `IpcStatus::StaleProcessHandle`.
  - Tested explicit token revocation on active processes, confirming clean `IpcStatus::PermissionDenied`.

## 4. Test Suite Execution
Target `stage238_13_ipc_concurrency_anti_replay_tests.exe` executed with zero failures.
