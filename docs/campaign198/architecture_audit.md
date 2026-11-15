# WinIOS Campaign 198: Architecture Audit Report

**Audit Target:** WinIOS Memory System, MMU Translation, Cache Architecture, and DMA Integration  
**Device Context:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Audit Date:** Campaign 198 (Completed Stage 198.19)  

---

## 1. Baseline Defect Remediation Audit

Prior to Campaign 198, several critical architectural defects existed in the memory and emulation subsystems:

| Component | Pre-Campaign 198 State | Campaign 198 Remediated State | Verification Suite |
| :--- | :--- | :--- | :--- |
| **`DC ZVA` Translation Bypass** | Wrote zeros directly to physical RAM without MMU translation or permission checks (`cpu.cpp:8295`). | Enforces virtual address translation, write permissions, Device memory rejection, and 64-byte block alignment via `UnifiedMemoryView`. | `stage198_1_unified_memory_tests`, `stage198_5_data_cache_tests` |
| **`DC CIVAC` Permission Check** | Grouped with `DC CVAC` (requiring Read permission only), allowing cache line invalidation without write authority. | Separated into dedicated handler requiring explicit Write permission (`AP[2] == 0`), throwing Data Abort (Permission Fault) on read-only pages. | `stage198_5_data_cache_tests` |
| **Device Memory Constraints** | Device memory treated as Normal unaligned RAM without enforcement of architectural atomic or alignment limits. | `decode_memory_attributes` decodes MAIR bytes; unaligned accesses and atomic RMWs on Device memory fault with Alignment / Abort. | `stage198_2_memory_attribute_tests`, `stage198_12_atomic_integration_tests` |
| **DMA Monitor Blindness** | `VirtualDmaEngine` writes did not invalidate multicore exclusive reservations on target ranges. | DMA transfers notify `SmpExclusiveMonitorManager::invalidate_reservations` across all cores for all affected 64-byte granules. | `stage198_9_dma_visibility_tests`, `stage198_10_dma_concurrency_tests` |
| **IC Invalidation at EL0** | Instructions `IC IALLU` / `IC IALLUIS` not strictly restricted at EL0. | Unconditionally trapped to EL1 with Undefined Instruction (`EC 0x00`) regardless of `SCTLR_EL1.UCI`. | `stage198_6_icache_sync_tests` |

---

## 2. Subsystem Conformance Summary

### A. MMU and Address Translation
- **Translation Granule:** 16 KB page size (`TCR_EL1.TG0 = 0b10`).
- **Translation Walk Level Starting Level:**
  - `T0SZ = 28` (36-bit address space): Walk starts at Level 2.
  - `T0SZ = 16` (48-bit address space): Walk starts at Level 0.
- **Cross-Core TLB Broadcasts:** `TLBI ALLE1IS`, `TLBI VAE1IS`, `TLBI ASIDE1IS`, and `TLBI VAAE1IS` propagate across all 8 cores in `SMPCluster`.

### B. Virtual Cache Hierarchy
- **Line Size:** 64 bytes for both instruction and data cache lines.
- **Cache Control Bits:** `SCTLR_EL1.DZE` controls EL0 access to `DC ZVA`; `SCTLR_EL1.UCI` controls EL0 access to `DC CVAU`, `DC CIVAC`, and `IC IVAU`.
- **Self-Modifying Code:** Full sequence `STR` -> `DC CVAU` -> `DSB ISH` -> `IC IVAU` -> `DSB ISH` -> `ISB` verified with guest execution.

### C. Large System Extensions (LSE) Atomics
- **Operations:** `CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET`.
- **Matrix Coverage:** 6 operation families $\times$ 4 operand widths (8, 16, 32, 64-bit) $\times$ 4 ordering variants (Relaxed, Acquire, Release, Acquire-Release) = 96 instruction variants tested with 100% pass rate.
- **Memory Coherency:** Atomic stores invalidate overlapping exclusive reservations and synchronize across striped locks.

### D. Synthetic DMA Engine
- **Descriptor Verification:** Full bounds, alignment, length limits, and cycle detection.
- **Concurrency:** Thread-safe parallel execution with CPU memory accesses.
- **Snapshot Support:** Full register and transfer state serialization into `.wsnap`.

---

## 3. Toolchain & Compilation Compliance
- **Compiler:** MSVC C++20 on Windows 11 x64.
- **Compiler Flags:** `/W4 /WX /permissive-` (zero warnings permitted, warnings treated as errors).
- **Secondary Compiler:** Zig 0.17.0 (`aarch64-macos`, freestanding Mach-O).
- **Build Status:** 100% clean compilation in both Release and Debug configurations.
