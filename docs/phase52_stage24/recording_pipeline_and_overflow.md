# WinIOS — Phase 52, Stage 24: Recording Pipeline and Buffer Overflow Policy

## 1. Overview
The SNDL v2 recording pipeline captures execution events across all active cores and global scheduling boundaries while enforcing strictly bounded memory consumption, deterministic I/O flush policies, and fail-safe buffer overflow handling.

## 2. In-Memory Buffering Architecture
The pipeline is managed by `SndlV2Writer` and `SndlV2Recorder`:
- **Current Block**: In-memory buffer `std::vector<SndlV2Record>` sized up to `config.records_per_block` (default 1024 records).
- **Block Header**: Tracks `first_global_sequence`, `last_global_sequence`, `record_count`, payload CRC32C, and block sequence ID.
- **Batched Disk Write**: When `current_block_.size() == records_per_block`, the block payload is serialized, CRC32C calculated, and written as a contiguous chunk to disk.

## 3. Strict Buffer Limit and Overflow Semantics
To prevent unbounded heap growth or memory exhaustion during high-frequency execution:
- `WriterConfig::max_buffer_records` specifies the absolute hard cap on total events permitted in a recording session.
- When `total_events_written_ + current_block_.size() >= config_.max_buffer_records`, the pipeline enters the `Overflow` state:
  1. The overflow flag is set: `overflow_occurred_ = true`.
  2. New incoming records are dropped immediately and accounted for via `dropped_events_count_++`.
  3. No partial or corrupted records are ever emitted to disk.
  4. On `finalize()`, the file footer is updated with `completion_status = CompletionStatus::Overflow` and `dropped_events = dropped_events_count_`.
  5. The recording file remains structurally valid and completely parseable up to the exact point of saturation.
  6. Validators detect `Overflow` as an incomplete session status without failing structural integrity.
