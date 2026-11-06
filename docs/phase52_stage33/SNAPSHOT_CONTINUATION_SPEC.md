# WinIOS — Snapshot Continuation Specification
## Mid-DMA Snapshot Prevention & Atomicity Safeguards

---

## 1. Problem Statement

If a snapshot capture (`.wsnap v2`) is attempted while a virtual DMA transfer is partially executed:
- Source and destination memory regions are in transient inconsistent states.
- Virtual DMA internal ring registers and descriptor pointers hold mid-packet counters.
- Exclusive monitors may have invalidation events in flight.

Restoring from such a snapshot would require restoring host device hardware thread micro-architectural state, which violates deterministic reproducibility.

---

## 2. Busy Gating & Mutual Exclusion Architecture

WinIOS resolves this via strict busy gating in `SmpSnapshotManager`:

```
Capture Request
      |
      v
+-------------------------------+
| Check DeviceBus Busy Status   |
| (VirtualDmaDevice::is_busy()) |
+---------------+---------------+
                |
       +--------+--------+
       |                 |
     Busy             Not Busy
       |                 |
       v                 v
[Reject Capture]  [Atomic Capture]
Error: DeviceBusy - Serialize CPU states
(Code: 0x05)      - Serialize Memory
                  - Serialize DeviceBus state
                  - Compute Canonical Hash
```

### Protocol Requirements
1. **Synchronous Quiescence:** Prior to capturing a `.wsnap v2` snapshot, all pending descriptor chains must complete, or the caller must wait until `is_busy() == false`.
2. **Explicit Error Code:** Attempting a snapshot while `is_busy()` returns true immediately aborts with `SnapshotErrorCode::DeviceBusy`. No partial snapshot files are written.
3. **Continuation Safety:** Once a clean snapshot is saved, execution or recording can continue immediately. Subsequent recordings bind their `base_snapshot_hash` to this newly generated snapshot.
