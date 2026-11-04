# WinIOS — Phase 52, Stage 25: Overflow, Crash and Recovery Semantics

## 1. Bounded Buffer Overflow Policy
When execution generates more events than the configured in-memory capacity (`max_buffer_records`):
- `SndlV2Writer::overflow_` is flagged.
- Subsequent events increment `dropped_events_` without allocating memory or corrupting written blocks.
- On `finalize()`, `completion_status` is written as `SndlV2CompletionStatus::Overflow` and `dropped_events` is accurately recorded in the footer.
- The validator ensures that a file marked `Overflow` declares $> 0$ dropped events, and a file marked `Completed` declares $0$ dropped events.

## 2. Crash Interruption & Incomplete Log Safety
If host emulation terminates unexpectedly (e.g. process crash, power interruption, disk full):
- The file will lack a valid footer.
- `SndlV2Validator::validate_file` detects missing footer and returns `SndlV2ErrorCode::IncompleteRecording`.
- An incomplete file is **never reported as clean or completed**.

## 3. Safe Recovery (`recover_incomplete_recording`)
Stage 25 introduces `SndlV2Validator::recover_incomplete_recording(input_path, recovered_path)`:
- Scans `input_path` block-by-block, validating block header CRC and payload CRC.
- Copies all valid complete blocks to `recovered_path`.
- Synthesizes a valid footer marked explicitly as `SndlV2CompletionStatus::Aborted` (never `Completed`).
- Leaves `input_path` completely untouched.
- Allows post-mortem analysis of execution up to the exact instant of interruption.
