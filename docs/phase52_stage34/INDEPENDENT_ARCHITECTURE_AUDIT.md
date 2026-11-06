# WinIOS — Phase 52, Stage 34 Architecture Audit
## Independent Correctness Audit of Deterministic Multicore Execution, Recording & Recovery

**Project:** WinIOS  
**Phase:** 52, Stage 34  
**Date:** November 2026  
**Auditor:** Independent Validation & Verification System  
**Pinned Inferno SHA:** `cc4302a99167abec69b714cfd00c38caece7e7de`

---

## 1. Scope & Methodology

This audit examines the actual source code, header definitions, and test suites across Stages 26–33 of WinIOS. It does not rely on prior narrative reports or assumptions of correctness.

Each of the following 18 architectural subsystems was independently audited:
1. AArch64 CPU execution and retirement (`src/cpu/cpu.cpp`, `src/cpu/cpu.h`)
2. SMP scheduler and quantum boundaries (`src/cpu/smp_scheduler.cpp`, `src/cpu/smp_scheduler.h`)
3. Global event ordering (`src/sndl/sndl_v2_recorder.cpp`, `src/cpu/smp_scheduler.cpp`)
4. Per-core event ordering (`src/cpu/smp_scheduler.cpp`, `src/replay/smp_replay_coordinator.cpp`)
5. Shared memory (`src/cpu/smp_shared_memory.cpp`, `src/cpu/smp_shared_memory.h`)
6. Exclusive monitor reservations (`src/cpu/smp_exclusive_monitor.cpp`, `src/cpu/smp_exclusive_monitor.h`)
7. Virtual interrupt routing (`src/cpu/smp_interrupt_router.cpp`, `src/cpu/smp_interrupt_router.h`)
8. Virtual timers and time sources (`src/cpu/virtual_time.cpp`, `src/cpu/virtual_time.h`)
9. MMIO device bus (`src/device/virtual_device_bus.cpp`, `src/device/virtual_device_bus.h`)
10. Virtual DMA engine (`src/device/virtual_dma_engine.cpp`, `src/device/virtual_dma_device.cpp`)
11. DMA event recording (`src/device/virtual_dma_device.cpp`, `src/sndl/sndl_v2_recorder.cpp`)
12. WDMA companion payload storage (`src/device/dma_payload_store.cpp`, `src/device/dma_payload_store.h`)
13. SNDL v2 validation (`src/sndl/sndl_v2_validator.cpp`, `src/sndl/sndl_v2_reader.cpp`)
14. Recording recovery (`src/device/recording_recovery.cpp`, `src/device/recording_recovery.h`)
15. Snapshot capture and restore (`src/snapshot/smp_snapshot_manager.cpp`, `src/snapshot/smp_snapshot_types.h`)
16. Replay checkpoint creation and continuation (`src/replay/smp_replay_coordinator.cpp`, `src/replay/smp_replay_types.h`)
17. Canonical cluster hashing (`src/cpu/smp_scheduler.cpp`, `src/cpu/smp_core_state.cpp`)
18. Error and divergence handling (`src/replay/smp_replay_coordinator.cpp`)

---

## 2. Findings Summary Table

| Finding ID | Severity | Subsystem | Affected Location | Summary |
|---|---|---|---|---|
| **AUD-34-01** | **HIGH** | Canonical Hashing | `src/cpu/smp_scheduler.cpp:compute_canonical_multicore_hash()` | DeviceBus (section 0x18), exclusive monitors, and interrupt router states omitted from cluster hash |
| **AUD-34-02** | **MEDIUM** | Payload Storage | `src/device/dma_payload_store.cpp:open_read()` | Unbounded vector allocation (`rec.data.resize(length)`) on corrupted or adversarial payload length |
| **AUD-34-03** | **MEDIUM** | Exclusive Monitor | `src/cpu/smp_exclusive_monitor.cpp:invalidate_reservations()` | Potential 64-bit integer overflow in address range check `(addr + size)` |
| **AUD-34-04** | **MEDIUM** | Recovery Scanner | `src/device/recording_recovery.cpp:recover()` | Discarding `DmaSubmissionAccepted` without verifying whether memory writes were already committed |
| **AUD-34-05** | **LOW** | Testing Rigor | `tests/unit/dma_recording_replay_tests.cpp` | Determinism tests compare record vs replay hash without an independent oracle calculating expected architectural state |
| **AUD-34-06** | **LOW** | Interrupt Router | `src/cpu/smp_interrupt_router.cpp:route_spi()` | Core target affinity mask limited to 8 bits (`uint8_t`), preventing topologies beyond 8 cores without truncation |

---

## 3. Detailed Audit Findings

### Finding AUD-34-01: Canonical Cluster Hash Hashing Gap
- **Severity:** HIGH
- **Affected Source:** `D:\WinISO26\WinIOS\src\cpu\smp_scheduler.cpp:218-242`
- **Description:** `compute_canonical_multicore_hash()` computes an FNV-1a hash over:
  1. `registered_core_count_`
  2. `global_retirement_sequence_`
  3. Per-core architectural register state (`slot.state.compute_canonical_hash()`)
  4. Shared guest RAM CRC32C
  However, it **omits** all VirtualDeviceBus device state (e.g. DMA device registers, pending transfer states, interrupt status), exclusive monitor reservation states, and virtual interrupt router pending queues.
- **Consequence:** If two executions diverge solely in peripheral register state (e.g., DMA status register flag or pending interrupt line) while core registers and memory match, the canonical cluster hash will falsely report identity ($H_1 == H_2$).
- **Reproducible Test Case:** Configure VirtualDmaDevice with pending status bit, clear bit on second run; compare `compute_canonical_multicore_hash()`.
- **Recommended Action:** Incorporate device bus hash and exclusive reservation state into `compute_canonical_multicore_hash()`, or maintain an augmented full-system hash for differential certification.

---

### Finding AUD-34-02: Unbounded Vector Allocation in Companion Payload Reader
- **Severity:** MEDIUM
- **Affected Source:** `D:\WinISO26\WinIOS\src\device\dma_payload_store.cpp:210-218`
- **Description:** In `DmaPayloadStore::open_read()`, the record entry header length field is decoded directly into a 64-bit integer:
  ```cpp
  const std::uint64_t length = trace::decode_u64_le(entry_hdr + 32);
  rec.data.resize(static_cast<std::size_t>(length));
  ```
  No validation is performed to bound `length` against maximum supported DMA burst sizes (e.g. `kMaxDmaTransferSize = 16 * 1024 * 1024`).
- **Consequence:** An adversarial or bit-flipped `.dma` file specifying `length = 0xFFFFFFFF00000000` causes an immediate `std::bad_alloc` unhandled exception, terminating the host process instead of returning structured error `DmaPayloadStoreError::InvalidLength`.
- **Reproducible Test Case:** Fuzz `.dma` file entry header length field with `0x7FFFFFFF` on a machine with limited memory.
- **Recommended Action:** Add guard: `if (length > kMaxSupportedPayloadSize) return fail(DmaPayloadStoreError::PayloadTooLarge);`

---

### Finding AUD-34-03: Potential Integer Overflow in Exclusive Monitor Range Check
- **Severity:** MEDIUM
- **Affected Source:** `D:\WinISO26\WinIOS\src\cpu\smp_exclusive_monitor.cpp:65-78`
- **Description:** In `is_reservation_valid` and `invalidate_reservations`:
  ```cpp
  const std::uint64_t end_paddr = paddr + size;
  ```
  If `paddr` is near `0xFFFFFFFFFFFFFFFFull` and `size` is non-zero, `paddr + size` wraps around zero, bypassing reservation invalidation checks.
- **Consequence:** Exclusive reservations at the top of 64-bit physical address space would fail to invalidate on overlapping writes.
- **Recommended Action:** Validate `paddr <= UINT64_MAX - size` before checking overlap.

---

### Finding AUD-34-04: Potential Committed State Loss on Incomplete Trailing DMA Trimming
- **Severity:** MEDIUM
- **Affected Source:** `D:\WinISO26\WinIOS\src\device\recording_recovery.cpp:142-160`
- **Description:** `RecordingRecoveryScanner` trims backward to the last complete transaction if `trim_incomplete_trailing_dma` is enabled:
  ```cpp
  if (rec.event_type == sndl::SndlV2EventType::DmaSubmissionAccepted) {
      salvaged_records.resize(last_sub_idx);
  }
  ```
  In virtual DMA execution, descriptor processing occurs synchronously. If host process crashes after memory write but before `DmaTransferCompleted` is flushed to disk, the recording recovery drops the `DmaSubmissionAccepted` event.
- **Consequence:** When replaying from base snapshot, the salvaged recording will not replay that DMA transfer. If another core had already observed the modified RAM within the same quantum, the trimmed replay could diverge unless the entire quantum is also rolled back.
- **Validation in Stage 33:** Stage 33 mitigates this by looping the trimmer across both DMA and open quantums. However, the audit notes that recovery must verify that no committed effects exist past the salvage boundary.
- **Recommended Action:** Add cross-check between salvaged RAM state and trailing DMA descriptor status.

---

### Finding AUD-34-05: Absence of Independent Reference Oracle in Existing Test Suite
- **Severity:** LOW
- **Affected Source:** `tests/unit/dma_recording_replay_tests.cpp`, `tests/unit/smp_eight_core_replay_tests.cpp`
- **Description:** Existing test suites verify determinism by running live execution, recording SNDL/WDMA streams, replaying them, and asserting:
  ```cpp
  TEST_ASSERT(replay_hash == live_hash);
  ```
  While this verifies that replay reproduces execution, it does not prove that execution calculated the correct architectural result according to ARMv8-A specifications or DMA descriptor semantics.
- **Consequence:** A bug present in both execution and replay (e.g. incorrect flag computation in an instruction, or inverted fill pattern) would pass all existing tests.
- **Recommended Action:** Implement Phase B Independent Reference Model and Phase C Differential Testing.

---

### Finding AUD-34-06: 8-Core Mask Limit in Interrupt Router
- **Severity:** LOW
- **Affected Source:** `D:\WinISO26\WinIOS\src\cpu\smp_interrupt_router.h`
- **Description:** SPI target affinity uses `std::uint8_t target_cores_mask`.
- **Consequence:** Maximum supported SMP core count is strictly bounded to 8 virtual cores. Topologies of 16 or 32 cores cannot be supported without expanding to `std::uint32_t`.
- **Recommended Action:** Keep explicit capability gating rejecting topologies > 8 cores until router mask is widened.

---

## 4. Audit Verdict & Sign-Off

The WinIOS deterministic multicore and virtual DMA architecture is fundamentally sound, rigorously structured, and free of critical memory-safety flaws in the core execution paths. The 6 identified findings provide actionable targets for hardening in Stage 34.

Proceed to **Phase B: Independent Reference Execution**.
