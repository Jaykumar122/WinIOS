# WinIOS Campaign 201 — Stage 201.18 to 201.20 Platform Replay Validation

**Campaign:** 201  
**Stages:** 201.18 (Restore Testing), 201.19 (Corruption & Atomicity), 201.20 (Deterministic Replay)  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Independent validation of full platform state restoration, corruption resilience, and bit-exact deterministic replay  
**Date:** November 16, 2026  

---

## 1. Executive Summary

Stages 201.18 through 201.20 validated the integrity, resilience, and determinism of the WinIOS state snapshot and replay pipeline on an Apple A16 Bionic 6-core cluster topology.

Key conclusions:
1. **Complete Component Restoration:** Verified that state restoration reconstructs not only CPU general-purpose registers, but also stack pointers, PSTATE flags, system registers (`SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`), PAC keyrings, and sparse RAM pages across the cluster.
2. **Corruption Rejection & Restore Atomicity:** Demonstrated that truncated snapshot buffers, corrupted magic signatures, and tampered payload bytes (detectable via CRC32C mismatches) are safely trapped and rejected. Crucially, a failed restore aborts cleanly without polluting or corrupting live running platform structures.
3. **Bit-Exact Replay:** Demonstrated that executing a multi-core workload, capturing an initial checkpoint, resetting, and re-executing results in **100% bit-for-bit identical** CPU registers, program counters, memory writes, and retirement counts across all 6 cores.

---

## 2. Replay Verification Matrix

| Validation Phase | Test Target / Scenario | Expected Invariant | Observed Outcome | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Complete Restore** | 6 Cores, RAM, PAC, SysRegs | All state elements match | Exact match across all cores | **PASS** |
| **Truncation Check** | Buffer truncated to 16 bytes | Validator rejects buffer | `val.valid == false` | **PASS** |
| **Magic Check** | Modified magic header bytes | Header parsing rejects file | `val.valid == false` | **PASS** |
| **CRC32C Integrity** | Single-byte payload bitflip | Section digest mismatch triggers abort | Restore fails, error reported | **PASS** |
| **Restore Atomicity** | Attempted restore with bad file | Live cluster registers & PC unaltered | Sentinel values preserved | **PASS** |
| **Bit-Exact Replay** | 50,000 instructions across 6 cores | Run 1 final state == Run 2 final state | 100% bit-exact equivalence | **PASS** |

---

## 3. Test Suite Reference
- Executable 1: `stage201_18_snapshot_restore_tests`
- Executable 2: `stage201_19_snapshot_corruption_tests`
- Executable 3: `stage201_20_deterministic_replay_tests`
