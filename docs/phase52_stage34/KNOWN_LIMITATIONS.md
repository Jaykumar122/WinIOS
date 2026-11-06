# WinIOS Phase 52 Stage 34: Known Limitations & Stage 35 Recommendations

## 1. Executive Summary

Phase 52 Stage 34 completed independent architecture auditing, differential testing against an isolated reference model, long-duration CPU and DMA endurance certification, and crash recovery validation.

This document details the architectural findings, boundary limits, and known limitations identified during the Stage 34 independent audit, along with concrete recommendations for Phase 52 Stage 35.

---

## 2. Identified Audit Findings & Limitations

### 1. AUD-34-01: Canonical Cluster Hash Excludes Device State & Router Queues
- **Severity:** Medium
- **Description:** `compute_canonical_multicore_hash()` calculates its hash solely from core registers and physical memory. While Section `0x18` of `.wsnap v2` captures device registers, device bus state and interrupt router internal pending queues are omitted from the single 64-bit canonical cluster hash.
- **Impact:** Divergences restricted entirely to device registers or in-flight virtual interrupts between quantums could escape hash comparison unless explicit snapshot section validation is run.
- **Recommendation for Stage 35:** Introduce an extended cluster hash `compute_extended_cluster_hash()` that incorporates device bus section `0x18` state and GIC router queue CRCs into the top-level verification hash.

### 2. AUD-34-02: Unvalidated Allocation Sizing in Companion Payload Store
- **Severity:** Low
- **Description:** `DmaPayloadStore::open_read()` reads a 64-bit payload length from the companion store header and calls `std::vector::resize()` without an upper bound sanity check against host physical memory.
- **Impact:** Fuzzed or maliciously corrupted companion files could induce `std::bad_alloc` or excessive heap commitment.
- **Recommendation for Stage 35:** Enforce a maximum payload allocation limit (e.g., 256 MiB) during companion header parsing before memory allocation.

### 3. AUD-34-03: Exclusive Monitor Address Range Integer Overflow Edge Case
- **Severity:** Low
- **Description:** In `ExclusiveMonitor::check_and_invalidate(paddr, size)`, the expression `paddr + size` does not explicitly check for 64-bit integer wraparound if `paddr` is near `UINT64_MAX`.
- **Impact:** Extreme edge-case addresses could wrap around to low memory addresses and fail to invalidate reservations.
- **Recommendation for Stage 35:** Add saturating arithmetic or check `(paddr + size < paddr)` before range evaluation.

### 4. Topographical Ceiling: Eight Cores Maximum
- **Severity:** Informational
- **Description:** The current scheduler, snapshot format, and replay cursor explicitly support topologies of 1, 2, 4, and 8 virtual cores. Topologies exceeding 8 cores (e.g. 16 or 32 cores) are rejected by design.
- **Status:** Intentional design boundary matching current architecture goals.

---

## 3. Recommendations for Phase 52 Stage 35

1. **Unify Extended Cluster Hash:** Implement comprehensive state hashing covering Section `0x18` device bus state, interrupt router queues, and timer comparator timestamps.
2. **Harden Companion Allocator:** Enforce bounded allocation sizes during companion payload deserialization.
3. **Advanced Peripheral Simulation:** Expand virtual device capabilities beyond basic DMA to include MMIO-driven virtio or emulated serial controllers under the same deterministic recording guarantees.
