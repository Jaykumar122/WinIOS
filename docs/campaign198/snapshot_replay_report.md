# WinIOS Campaign 198: Snapshot & Deterministic Replay Report

**Target Research Architecture:** ARMv8.1-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Storage Formats:** `.wsnap` (v2 System Snapshot) & `.sndl` (v2 Deterministic Log)  
**Classification:** `GUEST_MEMORY_MODEL` / `SYNTHETIC_KERNEL`  

---

## 1. Executive Summary

Campaign 198 Stages 14 and 15 complete the integration between the unified guest memory system, the virtual cache architecture, the synthetic DMA engine, and the `.wsnap`/`.sndl` snapshot and deterministic replay framework. System snapshots serialize full memory state, translation table entries, memory attributes (`MAIR_EL1`, `TCR_EL1`), and device controller states. Deterministic replay reproduces interleaved multicore memory operations, atomic RMW instructions, DMA completions, and cache barrier sequences with 100% bit-exact state matching.

---

## 2. Memory System Snapshot Architecture

The snapshot system (`SnapshotManager` and `SmpSnapshotManager`) captures all architectural and micro-architectural simulation state into a structured, CRC32C-verified `.wsnap` format:
- **CPU & MMU Registers:** Serializes all 31 general-purpose registers, banked stack pointers (`SP_EL0`..`SP_EL3`), PSTATE (`NZCV`, `DAIF`), vector registers (`V0`..`V31`), FP status/control registers (`FPCR`, `FPSR`), exception registers (`ELR_ELx`, `ESR_ELx`, `FAR_ELx`), and MMU configuration registers (`SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`, `MAIR_EL1`, `PAR_EL1`).
- **Physical Memory Buffers:** Serializes physical RAM contents in uncompressed or zlib-compressed form with 32-bit checksums per section.
- **Synthetic Device State:** `VirtualDmaDevice` state (MMIO control/status registers, descriptor address pointers, transfer statistics, and queue depth) is serialized via dedicated section descriptors.
- **Integrity Validation:** Headers, section tables, and payload chunks are protected by CRC32C checksums, detecting truncation, bit-flips, or version mismatch during admission.

---

## 3. Deterministic Multicore Replay Engine

The deterministic replay engine enables reproducible debugging of complex concurrent memory anomalies:
- **Thread Interleaving Recording:** Interleaved memory operations across cores are sequenced and timestamped using virtual time step counters.
- **Event Classification:** Reads, writes, acquire/release barriers, atomic RMWs (`CAS`, `SWP`, `LDADD`, etc.), and DMA transfers are recorded into the `.sndl` event journal.
- **State Hash Verification:** At key execution milestones and at replay termination, a cumulative 64-bit FNV-1a hash is computed from the retiring register values, memory updates, and DMA transfer outcomes.
- **Repeatability:** Independent replay executions starting from the same base `.wsnap` snapshot and reading the same `.sndl` recording produce identical state hashes (0 discrepancies across all test runs).

---

## 4. Verification Evidence

- `stage198_14_snapshot_memory_tests`:
  - CPU & MMU system register round-trip verified (100% register preservation).
  - Full system memory & CPU snapshot capture and restore verified.
  - `VirtualDmaDevice` serialization & deserialization round-trip verified.
- `stage198_15_deterministic_replay_tests`:
  - 5 pseudo-random seeds evaluated across 3 independent simulation runs each; 100% identical state hashes generated.
  - Distinct seeds verified to generate distinct valid deterministic traces.
