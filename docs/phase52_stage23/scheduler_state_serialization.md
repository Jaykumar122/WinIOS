# Scheduler & Global Execution-State Serialization

## 1. Overview

The `SCHEDULER` (Section Type `0x14`) serializes the deterministic execution coordinator state maintained by `SmpScheduler`.

To achieve bit-exact deterministic continuation, the scheduler state preserves:
1. Configured instruction quantum budget $Q$.
2. Continuous monotonic global retirement sequence clock $S_{\text{global}}$.
3. Registered core count $N$.
4. Current round-robin core index $idx_{\text{current}}$.
5. Per-slot retirement statistics and execution metrics.

---

## 2. Binary Layout

### Header (32 Bytes)
- `quantum_instructions` (u64): Maximum instruction retirement budget per scheduling slice.
- `global_retirement_sequence` (u64): Monotonic instruction counter retired across all cluster cores.
- `registered_core_count` (u32): Count of active registered cores.
- `current_core_index` (u32): Next core index to inspect in round-robin selection.
- `slot_count` (u32): Number of slot metrics entries in payload.
- `reserved` (u32): Padding.

### Slot Metrics Entries ($32 \times \text{slot\_count}$ Bytes)
For each registered slot:
- `core_id` (u32)
- `active` (u32, 1 = true, 0 = false)
- `total_instructions_retired` (u64): Instructions retired by this virtual core.
- `quantums_allocated` (u64): Total scheduling slices allocated to this core.
- `early_truncations` (u64): Count of quantum slices truncated by WFI, WFE, HLT, or Exception.

---

## 3. Restoration & Deterministic Continuation

When restored:
1. Target `SmpScheduler` restores `quantum_instructions_`, `global_retirement_sequence_`, and `current_core_index_`.
2. Each virtual core slot updates its retirement and quantum allocation metrics.
3. The scheduler resumes execution from the exact next core in round-robin sequence without reordering or skipping.

---

## 4. Verification

Test Group D (`D1_SchedulerStateRoundTrip`) validates full round-trip preservation of all metrics. Test Group I (`Deterministic Execution Continuation`) confirms that continuing execution from a restored scheduler yields bit-exact identical schedules over 10 repeated runs across 2-, 4-, and 8-core configurations.
