# WinIOS — Replay Checkpoint Specification
## Mid-Stream State Serialization & Fast-Forward Resumption

---

## 1. Motivation

In long-running deterministic SMP replays (e.g., simulating hours of execution or millions of DMA transactions), replaying from $t=0$ on every debug iteration is prohibitively expensive.

`SmpReplayCheckpoint` provides periodic mid-replay state capture, enabling:
- O(1) jump to arbitrary quantum boundaries.
- Bi-directional debugging and time-travel inspection.
- Regression isolation around specific failure points.

---

## 2. Checkpoint Data Layout

```
+-------------------------------------------------------------+
| SmpReplayCheckpoint Structure                               |
|                                                             |
|  - uint64_t magic               = 0x5743484B504E5431        |
|  - uint64_t checkpoint_id       = Monotonic ID              |
|  - uint64_t global_sequence     = Retirement sequence       |
|  - uint64_t active_quantum      = Current quantum number    |
|  - uint32_t active_core_id      = Currently scheduled core  |
|  - uint64_t sndl_file_offset    = Sndl reader byte offset   |
|  - uint64_t sndl_record_index   = Next record index         |
|  - uint64_t dma_payload_seq     = Current DMA sequence      |
|                                                             |
| Embedded Sections:                                          |
|  - CPU Register State (Cores 0..7)                          |
|  - GICv3 Interrupt Router State                             |
|  - SmpSharedMemory Dirty/RAM State                          |
|  - Exclusive Monitor Reservations                           |
|  - DeviceBus & VirtualDmaDevice State                       |
|  - Integrity Hash: Canonical Cluster Hash at Checkpoint     |
+-------------------------------------------------------------+
```

---

## 3. Restoration & Hash Parity Verification

When `coordinator.restore_checkpoint(ckpt)` is called:
1. All CPU registers, program counters, and cycle counters are restored to the checkpoint state.
2. Shared RAM is updated to match the checkpoint image.
3. Sndl and WDMA stream cursors are sought directly to `sndl_file_offset` and `sndl_record_index`.
4. Replay execution resumes.

**Parity Guarantee:**
$$\text{FinalHash}_{\text{ResumedFromCheckpoint}} \equiv \text{FinalHash}_{\text{ContinuousReplay}}$$
Empirically proven across 4-core and 8-core configurations with exact bitwise matching (`0xc2bb6527cebf255c`).
