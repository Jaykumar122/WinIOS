# WinIOS Phase 52 Stage 18: Recording Subsystem Implementation

## 1. Subsystem Architecture

The recording engine is implemented primarily across four core classes in `src/sndl/`:

```
           +-------------------------------------------------+
           |           Global Active Recorder Hook           |
           |   g_active_recorder (std::atomic<SndlRecorder*>) |
           +-------------------------------------------------+
                                    |
                                    v
           +-------------------------------------------------+
           |                 SndlRecorder                    |
           | * Lifecycle management (start/stop/abort)       |
           | * Atomic global sequence allocation             |
           | * Per-core sub-instruction tracking             |
           | * Bounded buffering & overflow detection        |
           +-------------------------------------------------+
                                    |
                                    v
           +-------------------------------------------------+
           |                  SndlWriter                     |
           | * Block batching (1024 records/block)           |
           | * Header/Block/Footer serialization             |
           | * Synchronous & thread-safe disk flushes        |
           | * Intel CRC32C payload calculation              |
           +-------------------------------------------------+
```

---

## 2. Component Details

### 2.1 SndlRecorder (`src/sndl/sndl_recorder.h` & `.cpp`)
The central orchestrator for recording sessions:
- **`start_recording(path, core_count, base_snapshot_hash, snapshot_id)`**:
  - Initializes writer and opens output file.
  - Generates initial 64-byte `WSndlHeader` with system timestamp, core count, and snapshot bindings.
  - Resets sequence counters, retirement coordinates, and dropped record counts.
  - Transitions lifecycle state to `Recording`.
- **`submit_record(SndlRecord record)`**:
  - Thread-safe submission path protected by mutex.
  - Assigns monotonic `sequence_num = ++sequence_counter_`.
  - Computes per-core `sub_instruction_ordinal`.
  - Checks bounded capacity: if `total_recorded_ >= queue_capacity_` (when bounded) or if overflow previously occurred, increments `dropped_records_`, marks `overflowed_ = true`, sets state to `Failed`, and returns `SndlResult::fail(BufferOverflow)`.
  - Forwards valid records to `SndlWriter::write_record()`.
- **`stop_recording(total_instructions)`**:
  - Flushes active block buffer to disk.
  - If overflow occurred, writes `WSndlFooter` with `finalization_status = Overflow` and returns failure.
  - If successful, writes `WSndlFooter` with `finalization_status = Completed`.
  - Updates file header with final record and instruction totals.
  - Sets state to `Completed`.
- **`abort_recording()`**:
  - Emergency abort path that finalizes file with `finalization_status = Aborted` and closes writer.

### 2.2 SndlWriter (`src/sndl/sndl_writer.h` & `.cpp`)
A low-overhead, block-oriented disk sink:
- Batches incoming records into an in-memory block buffer of size `max_records_per_block_` (default 1024 records = 64 KiB payload).
- When the buffer reaches 1024 records:
  1. Computes payload CRC32C using Castagnoli polynomial.
  2. Encodes 48-byte `WSndlBlockHeader` (`SBLK`).
  3. Writes block header and 64 KiB record payload to disk in a single sequential I/O operation.
  4. Resets block buffer.
- On `finalize()`:
  - Flushes any partially filled block (e.g. 1..1023 records).
  - Encodes and appends 64-byte `WSndlFooter` (`WSNDLFTR`).
  - Seeks to offset 0 and rewrites the updated `WSndlHeader` with final record counts and updated CRC32C.

### 2.3 Inline Non-Deterministic Source Hooks (`src/sndl/sndl_source_hooks.h` & `.cpp`)
Provides zero-overhead inline hook entrypoints:
- `hook_mmio_read(core_id, pc, ret_count, paddr, val, width, source_id)`
- `hook_interrupt_inject(core_id, intid, priority, flags, source_id)`
- `hook_interrupt_delivery(core_id, pc, ret_count, intid, priority, is_fiq, is_virtual, target_el)`
- `hook_timer_expiration(core_id, pc, ret_count, timer_type, cval, counter)`
- `hook_counter_read(core_id, pc, ret_count, reg_key, counter_val, frequency)`
- `hook_entropy(core_id, pc, ret_count, val, bit_width, source_id)`
- `hook_host_input(core_id, channel_id, data_payload, length)`

Each hook checks `get_active_recorder()`:
```cpp
void hook_mmio_read(...) noexcept {
    if (auto* rec = get_active_recorder()) {
        rec->record_mmio_read(...);
    }
}
```
If inactive, the hook is a single CPU load instruction that evaluates to false and returns immediately.

---

## 3. Strict Overflow Policy

In deterministic replay systems, an incomplete log is completely unusable. Silent event loss causes replay divergence and catastrophic hard-to-debug crashes.

Stage 18 enforces a strict overflow policy:
1. When internal queues saturate, the recorder immediately flags `overflowed_ = true`.
2. All subsequent events are counted in `dropped_records_` rather than logged.
3. The recording state transitions to `SndlLifecycleState::Failed`.
4. On file finalization, the footer is marked with status `SndlFinalizationStatus::Overflow`.
5. Any subsequent validation with `win_ios sndl validate` or `SndlValidation::validate_file()` rejects the recording with `SndlErrorCode::BufferOverflow`.
