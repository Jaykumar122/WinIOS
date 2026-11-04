# WinIOS Phase 52, Stage 19 — Divergence Classification and Root-Cause Diagnostics

## 1. Zero-Drift Policy and Halt-on-First-Divergence

WinIOS deterministic replay enforces a strict **zero-drift** policy:
- Replay does not attempt best-effort heuristic continuation or silent fallback when an execution diverges from the recorded log.
- Upon detecting the first discrepancy in sequence, boundary, memory address, register value, or architectural state, execution is stopped **immediately** at that exact retirement count and sub-instruction boundary.
- A comprehensive JSON report (`divergence_report.json`) is emitted containing full diagnostic context.

---

## 2. Complete Taxonomy of 16 Divergence Categories

`ReplayDivergenceCategory` defines 16 explicit categories covering all failure and mismatch modes:

| ID | Category Enum | Description & Trigger Conditions |
|---|---|---|
| **0** | `None` | Execution matches recording perfectly; no divergence. |
| **1** | `SnapshotMismatch` | The baseline `.wsnap` hash or UUID differs from the recording header. |
| **2** | `EventSequenceMismatch` | An unexpected event sequence number was encountered (e.g. sequence jump or reordering). |
| **3** | `BoundaryMismatch` | An event arrived at an unexpected architectural boundary (e.g., DuringInstruction vs. ExceptionBoundary). |
| **4** | `RetirementMismatch` | Current instruction retirement count $R$ does not match the recorded retirement count of the event. |
| **5** | `CoreMismatch` | The target core ID of the event differs from the currently executing core. |
| **6** | `EventTypeMismatch` | The event type encountered in the log does not match the architectural operation requested by CPU/MMU. |
| **7** | `MMIOAddressMismatch` | A guest MMIO access requested physical address $A_1$, but the log expected address $A_2$. |
| **8** | `MMIOWidthMismatch` | Access width mismatch (e.g. guest executed 32-bit load `LDR Wn` instead of 64-bit `LDR Xn`). |
| **9** | `InputValueMismatch` | Value consumed by guest differs from the recorded value (or log was prematurely exhausted). |
| **10** | `InterruptMismatch` | Interrupt delivery mismatch (e.g., unexpected INTID, priority level, or target EL). |
| **11** | `TimerMismatch` | Hardware timer expiration parameters or virtual counter offset mismatch. |
| **12** | `StateHashMismatch` | Architectural register or system register state hash does not match checkpoint/baseline. |
| **13** | `MemoryMismatch` | Guest RAM checksum differs from expected snapshot or checkpoint. |
| **14** | `UnsupportedEvent` | An unhandled or unknown event encoding was encountered in the stream. |
| **15** | `ReplayTimeout` | Replay execution exceeded configured maximum real-time duration limit (`max_replay_duration_ms`). |
| **16** | `InternalReplayError` | Unhandled host exception, file I/O corruption, or internal subsystem failure. |

---

## 3. Structured Diagnostics Report Schema (`divergence_report.json`)

The divergence report captures complete forensic evidence for debugging:

```json
{
  "session_id": "replay_session",
  "diverged": true,
  "execution_stats": {
    "instructions_executed": 4,
    "events_consumed": 1,
    "checkpoints_taken": 2,
    "elapsed_ms": 1.250
  },
  "divergence": {
    "category": "MMIOAddressMismatch",
    "category_id": 7,
    "sequence_num": 2,
    "core_id": 0,
    "pc": "0x0000000000001004",
    "retirement_count": 1,
    "sub_instruction_ordinal": 0,
    "expected_boundary": "DuringInstruction",
    "actual_boundary": "DuringInstruction",
    "expected_event_type": "MmioReadResult",
    "actual_event_type": "MmioReadResult",
    "expected_value": "0x000000001c000000",
    "actual_value": "0x000000001c000004",
    "message": "MMIO read address mismatch"
  },
  "expected_hashes": {
    "cpu_registers": "0x89abcdef01234567",
    "simd_registers": "0x123456789abcdef0",
    "system_registers": "0xfedcba9876543210",
    "memory": "0x0123456789abcdef",
    "timer": "0x5555555555555555",
    "gic": "0xaaaaaaaaaaaaaaaa",
    "combined": "0x7777777777777777"
  },
  "actual_hashes": {
    "cpu_registers": "0x89abcdef01234567",
    "simd_registers": "0x123456789abcdef0",
    "system_registers": "0xfedcba9876543210",
    "memory": "0x0123456789abcdef",
    "timer": "0x5555555555555555",
    "gic": "0xaaaaaaaaaaaaaaaa",
    "combined": "0x7777777777777777"
  }
}
```
