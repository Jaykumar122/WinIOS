# DMA and Host Wall-Clock Replay Blockers (BLK-SMP-03 & BLK-SMP-04)

## 1. Overview of Open Blockers

During Phase 52 Stage 25 readiness assessment, four primary blockers were identified:

| Blocker ID | Severity | Description | Stage 26 Resolution |
|---|---|---|---|
| **BLK-SMP-01** | CRITICAL | No multicore replay loop coordinating scheduler decisions and quantum bounds | **RESOLVED**: Implemented in `SmpReplayCoordinator` |
| **BLK-SMP-02** | CRITICAL | No multicore event-consumption loop synchronizing global and per-core retirement counters | **RESOLVED**: Implemented in `SmpReplayCoordinator` & `SmpReplayEventCursor` |
| **BLK-SMP-03** | HIGH | Asynchronous host-side DMA operations not modeled or captured in SNDL v2 stream | **ACTIVE BLOCKER**: Rejected by Capability Gate |
| **BLK-SMP-04** | HIGH | Non-deterministic host wall-clock reads bypassing simulated virtual counter (`CNTVCT_EL0`) | **ACTIVE BLOCKER**: Rejected by Capability Gate |

Stage 26 purposefully keeps **BLK-SMP-03** and **BLK-SMP-04** active and strictly rejected, safeguarding determinism against unmodeled host asynchronous effects.

---

## 2. Technical Analysis: BLK-SMP-03 (Host-Side DMA)

### The Problem

In a full-featured emulator or hypervisor, Direct Memory Access (DMA) engines (such as virtio-net, virtio-blk, or simulated NVMe controllers) transfer blocks of data directly to guest RAM concurrently with CPU instruction execution.

If host DMA operations occur asynchronously outside the deterministic scheduler:
1. Shared memory pages can be mutated at nondeterministic global retirement timestamps ($R_{global}$).
2. Cores executing load instructions (`LDR`, `LDREX`, etc.) observe memory values that depend on host thread scheduling and OS latency rather than guest execution step.
3. Recording these memory changes requires either:
   - Synchronous DMA emulation aligned to quantum boundaries.
   - Fine-grained memory access logging (recording every cacheline mutation with global retirement timestamps).

### Current Mitigation and Gate Policy

In Stage 26:
- All guest I/O in the supported 2-core profile is strictly programmed I/O (MMIO) captured as synchronous `MmioRead` events in SNDL v2.
- Any attempt to enable host-side DMA replay sets `permit_host_dma = true`, which is intercepted and rejected by `SmpReplayCoordinator::prepare()`:
  ```cpp
  if (cap_config_.permit_host_dma) {
      return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
          "BLK-SMP-03 violation: host DMA replay is unsupported");
  }
  ```

---

## 3. Technical Analysis: BLK-SMP-04 (Host Wall-Clock Bypass)

### The Problem

AArch64 guest code reads the virtual timer counter via `CNTVCT_EL0` and system registers. If an interpreter or device model queries the host operating system clock (e.g. `std::chrono::system_clock::now()`, `QueryPerformanceCounter()`) directly:
1. Elapsed time across execution steps will fluctuate between runs.
2. Guest timers and timeouts will trigger at differing instruction retirement counts.
3. Timer interrupt delivery events in SNDL v2 (`ExternalInterrupt`) would conflict with the core's local cycle counter.

### Current Mitigation and Gate Policy

In Stage 26:
- All timer calculations derive deterministically from retired instruction counts ($R_{core}$ and $R_{global}$) and synthetic monotonic frequency scaling.
- Direct host wall-clock querying is forbidden during deterministic replay.
- The capability gate strictly rejects `permit_host_wall_clock = true`:
  ```cpp
  if (cap_config_.permit_host_wall_clock) {
      return fail_prepare(ReplayErrorCode::UnsupportedConfiguration,
          "BLK-SMP-04 violation: host wall-clock replay is unsupported");
  }
  ```

---

## 4. Path to Resolution in Future Stages

1. **Resolution for BLK-SMP-03**:
   - Introduce a deterministic DMA controller model.
   - Constrain DMA execution to scheduler quantum boundaries ($Q_{end}$).
   - Emit SNDL v2 `DmaMemoryTransfer` events recording buffer offsets, lengths, and memory hashes.

2. **Resolution for BLK-SMP-04**:
   - Provide complete virtualization of `CNTVCT_EL0` tied strictly to deterministic virtual ticks.
   - Enforce architectural sandbox wrappers preventing calls to `QueryPerformanceCounter` or `std::chrono` from within guest emulation handlers.
