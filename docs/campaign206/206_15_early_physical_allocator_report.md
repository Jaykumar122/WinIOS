# Stage 206.15: Early Physical Allocator Report

## 1. Executive Summary
- **Stage:** 206.15 — Early Physical Allocator
- **Status:** PASS
- **Date:** 2026-11-16
- **Subsystem:** `EarlyPhysicalPageAllocator` (`src/runtime/early_kernel_services.h/.cpp`)
- **Key Verifications:** Deterministic memory stealing above `topOfKernelData`, power-of-two alignment, exhaustion bounds, and rollback on out-of-memory.

---

## 2. Allocation Lifecycle & Boundary Verification

| Test Scenario | Requested Allocation | Expected Alignment | Returned Physical Address | Watermark State | Status |
|---|---|---|---|---|---|
| Single Page (16 KB) | 1 page (0x4000) | 16 KB default | `0x100000` | Advanced to `0x104000` | PASSED |
| Custom Align (64 KB)| 2 pages (0x8000) | 64 KB (`0x10000`) | `0x110000` | Advanced to `0x118000` | PASSED |
| Overflow Rejection | 12 pages (0x30000) | 16 KB default | `0x0` (Rejected) | Unchanged at `0x118000` | PASSED |
| Exact Fit Fill | 10 pages (0x28000) | 16 KB default | `0x118000` | Reached limit `0x140000` | PASSED |
| Exhausted Pool Attempt| 1 page (0x4000) | 16 KB default | `0x0` (Rejected) | Stays at limit `0x140000` | PASSED |

---

## 3. Kernel Integration Summary
- Operates in physical DRAM between `topOfKernelData` and high DRAM limit.
- Ensures zero data corruption of loaded kernel segments or boot argument structures.
- Provides backing pages for initial page tables and early per-CPU data structures before full Mach VM initialization.
