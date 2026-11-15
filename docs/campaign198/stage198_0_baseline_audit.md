# WinIOS Campaign 198: Stage 198.0 Baseline Audit & Verification Report

**Date:** 2026-11-15  
**Campaign:** 198 — ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance  
**Baseline Commit:** `0791c76822c864c802da65893fc9671b3879b857` (Branch: `campaign-191r`)  
**Target Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`, 16KB page granule, 64B cache line)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  

---

## 1. Baseline Test Verification

Prior to any modifications in Campaign 198, full CTest regressions were executed in both Release and Debug configurations against the clean baseline commit `0791c76`.

### Test Suite Execution Summary
- **Release Configuration:**
  - Total Tests Registered: 415
  - Tests Executed: 414
  - Tests Passed: 414 (100% runnable pass rate)
  - Tests Skipped: 1 (`383 - inferno_integration_tests`, expected when Inferno submodule is not active)
  - Tests Failed: 0
  - Execution Time: 81.09 seconds
  - Log Record: `docs/campaign198/baseline_test_results_release.txt`

- **Debug Configuration:**
  - Total Tests Registered: 415
  - Tests Executed: 414
  - Tests Passed: 414 (100% runnable pass rate)
  - Tests Skipped: 1 (`383 - inferno_integration_tests`)
  - Tests Failed: 0
  - Execution Time: 222.17 seconds
  - Log Record: `docs/campaign198/baseline_test_results_debug.txt`

The test suite baseline is 100% green and regression-free.

---

## 2. Memory Subsystem Architectural Audit

An in-depth audit of the existing WinIOS memory, CPU, MMU, cache, and DMA implementations was conducted to identify architectural discrepancies, translation bypasses, and concurrency boundaries.

### 2.1 Existing Components
1. **`Memory` (`src/memory/memory.h`, `memory.cpp`):**
   - Flat guest physical RAM with 4096-byte stripe locking (`std::shared_mutex`).
   - Store and load observer callbacks for cache and monitor synchronization.
   - `std::recursive_mutex* store_mutex_` for global store serialization.

2. **`CheckedGuestMemory` (`src/cpu/cpu.cpp`):**
   - Scoped accessor created per instruction in `CPU::step`.
   - Performs Stage-1 / Stage-2 MMU translation via `cpu_.get_mmu().translate()`.
   - Enforces alignment check (`SCTLR_EL1.A` or instruction-mandated natural alignment).
   - Handles multi-byte accesses spanning 16KB page boundaries.
   - Dispatches GIC MMIO accesses (`cpu_.get_gic().is_mmio_address(pa)`).
   - Triggers PMU memory access events and hardware watchpoint checks (`check_watchpoint`).
   - Emits memory trace records.

3. **`mmu::MMU` (`src/cpu/mmu.h`, `mmu.cpp`):**
   - Implements ARMv8-A translation regimes (EL0/EL1, 16KB granules, TCR_EL1, TTBR0_EL1, TTBR1_EL1).
   - Resolves translation table descriptors (L0-L3), page permissions (AP[2:1], UXN, PXN), and memory attributes.

4. **`VirtualDmaEngine` (`src/device/virtual_dma_engine.h`, `virtual_dma_engine.cpp`):**
   - Models asynchronous memory-to-memory DMA transfers across virtual channels.
   - Uses `SmpSharedMemory` to read and write physical bytes directly.

---

## 3. Critical Architectural Gaps Identified

The audit identified three major architectural deficiencies that Campaign 198 must address:

1. **Cache Maintenance Address Translation Defect:**
   - In `src/cpu/cpu.cpp` lines 8251–8326, cache maintenance operations by virtual address (`DC ZVA`, `DC CVAC`, `DC CVAU`, `DC CIVAC`, `DC IVAC`, `IC IVAU`) invoke `memory.check_range(va, ...)` directly.
   - When Stage-1 MMU translation is active, `va` is a virtual address, but `memory.write_u64(block_va + offset, 0ULL)` in `DC ZVA` writes directly to physical memory at offset `block_va`. In userspace or higher-half kernel memory, this accesses out-of-range physical offsets or corrupts wrong physical pages.
   - Translation and permission checks (Read permission for Clean, Write permission for Invalidate/Zero) must be strictly performed via MMU translation prior to cache maintenance.

2. **Memory Attribute Disconnect (MAIR_EL1 & Device Types):**
   - Translation descriptors encode `AttrIndx[2:0]`, which indexes `MAIR_EL1` to select memory type (Normal vs. Device-nGnRnE / Device-nGnRE) and cacheability.
   - Currently, `CheckedGuestMemory` has only partial device-rejection logic. Device memory must strictly reject unaligned accesses with an Alignment Fault (FSC 0x21), must reject atomic operations, and must bypass CPU caches.

3. **CPU-DMA Coherency and Reservation Disconnect:**
   - DMA engine writes bypass the CPU's `SmpExclusiveMonitor`. When a DMA engine writes to an address reserved by a CPU via `LDXR`, the exclusive reservation is not invalidated, violating ARMv8-A multi-copy atomicity and coherency expectations.
   - CPU writes without cache clean (`DC CVAC` / `DC CIVAC`) or DMA writes without cache invalidation need deterministic coherency semantics.

---

## 4. Campaign 198 Action Plan

The remaining 19 stages of Campaign 198 will sequentially address these gaps:
- **Stages 198.1 – 198.3:** Unified memory access interface, full `MAIR_EL1` memory attribute integration, cross-core TLBI invalidation completion.
- **Stages 198.4 – 198.7:** Cache line state modeling, architectural DC/IC maintenance with MMU translation and permission enforcement, instruction cache pipeline synchronization, and multicore coherency protocol.
- **Stages 198.8 – 198.10:** DMA memory integration with boundary checks, CPU-DMA visibility/synchronization, exclusive monitor invalidation, and fault recovery.
- **Stages 198.11 – 198.13:** 8-core shared physical memory conformance, 96 LSE atomic operations under unified memory, and advanced litmus stress testing.
- **Stages 198.14 – 198.16:** Memory snapshot capture/restore, deterministic replay of memory/cache/DMA events, and crash recovery.
- **Stages 198.17 – 198.19:** Guest Mach-O workload execution (Zig 0.17.0 cross-compiled), differential validation against architectural references, and final closeout regression.
