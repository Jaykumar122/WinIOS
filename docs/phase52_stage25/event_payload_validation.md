# WinIOS — Phase 52, Stage 25: Event Payload Semantic Validation

## 1. Overview
Stage 25 hardens the `SndlV2Validator` by adding comprehensive semantic validation rules that prevent structurally valid files with semantically impossible or corrupted payload parameters from being accepted.

## 2. Validation Rules & Bounds

1. **Opcode Range Checking**:
   - Every record's `event_type` must map to a defined enum value in `SndlV2EventType`:
     - Core Lifecycle: `[10..16]`
     - Scheduler: `[20..25]`
     - Interrupts / Exceptions: `[30..34]`
     - Timers / PMU: `[40..43]`
     - IO / Entropy: `[50..52]`
     - Shared Memory / Exclusives: `[60..63]`
   - Any unknown opcode returns `SndlV2ErrorCode::MalformedRecord`.
2. **Boundary Type Checking**:
   - Must be in range `[1..9]` (`BeforeInstruction` through `CoreLifecycleBoundary`).
   - Boundary `0` (`Unknown`) or `> 9` returns `SndlV2ErrorCode::MalformedRecord`.
3. **Payload Bounds & Reserved Padding**:
   - `rec.payload_size` must be $\le 24$ bytes.
   - `rec.reserved` must be `0` when `check_reserved_fields` is enabled.
4. **Domain-Specific Constraints**:
   - `QuantumStart`: `payload1` (instruction quantum budget) must be $> 0$.
   - `StoreExclusiveResult`: `payload2` (status) must be strictly `0` (Success) or `1` (Failure).
   - `MmioReadResult`: `payload3` (access width in bytes) must be $\in \{1, 2, 4, 8\}$.
   - `CoreID`: `rec.core_id < header.core_count` (or `0xFFFFFFFF` for cluster-wide events).
