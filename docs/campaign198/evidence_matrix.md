# WinIOS Campaign 198: Evidence Matrix

**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Granule Profile:** 16 KB Page Translation Granule | 64-Byte Cache Line / Reservation Granule  
**Baseline Commit:** `0791c76822c864c802da65893fc9671b3879b857`  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 (MSVC C++20, `/W4 /WX /permissive-`), Zig 0.17.0  

---

## 1. Stage-by-Stage Verification Evidence

| Stage | Title | Verification Target | Test Executable | Status | Classification |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **198.0** | Baseline Audit | Audit existing translation paths, identify bypass defects | Baseline CTest (Release & Debug) | Passed (414 pass, 1 skip) | `GUEST_MEMORY_MODEL` |
| **198.1** | Unified Guest Memory | `UnifiedMemoryView`: central translation, permissions, alignment | `stage198_1_unified_memory_tests` | Passed (7/7 tests) | `GUEST_MEMORY_MODEL` |
| **198.2** | Memory Attributes | `decode_memory_attributes`: Device vs Normal, MAIR decode | `stage198_2_memory_attribute_tests` | Passed (4/4 tests) | `GUEST_MEMORY_MODEL` |
| **198.3** | Cross-Core Translation | Cross-core TLBI broadcast, ASID isolation, page reuse | `stage198_3_cross_core_translation_tests` | Passed (4/4 tests) | `GUEST_MEMORY_MODEL` |
| **198.4** | Cache Architecture Audit | CTR_EL0, DCZID_EL0, 64-byte line, PoC vs PoU domains | `docs/campaign198/stage198_4_cache_audit.md` | Completed | `GUEST_CACHE` |
| **198.5** | Data Cache Operations | DC ZVA, DC CVAC, DC CVAU, DC CIVAC, DC IVAC | `stage198_5_data_cache_tests` | Passed (3/3 tests) | `GUEST_CACHE` |
| **198.6** | Instruction Cache Sync | Self-modifying code protocol, IC IVAU, IC IALLU traps | `stage198_6_icache_sync_tests` | Passed (3/3 tests) | `GUEST_CACHE` |
| **198.7** | Multicore Coherency | Multi-copy atomicity, DMB/DSB ordering, exclusive monitors | `stage198_7_coherency_protocol_tests` | Passed (3/3 tests) | `GUEST_MEMORY_MODEL` |
| **198.8** | DMA Memory Integration | `VirtualDmaEngine`: limits, bounds, address overflow, chaining | `stage198_8_dma_memory_tests` | Passed (4/4 tests) | `GUEST_DMA` |
| **198.9** | CPU-DMA Visibility | CPU writes -> DC clean -> DMA reads; DMA writes invalidate monitor | `stage198_9_dma_visibility_tests` | Passed (4/4 tests) | `GUEST_DMA` |
| **198.10** | DMA Concurrency & Faults | Concurrent CPU/DMA, cyclic descriptor chain recovery | `stage198_10_dma_concurrency_tests` | Passed (3/3 tests) | `GUEST_DMA` |
| **198.11** | 8-Core SMP Conformance | 8-core shared RAM, 8-way exclusive reservations, PSCI boot | `stage198_11_eight_core_memory_tests` | Passed (4/4 tests) | `SYNTHETIC_KERNEL` |
| **198.12** | 96 Atomic LSE Variants | All 96 LSE atomic operations (CAS, SWP, LDADD, LDCLR, LDEOR, LDSET) | `stage198_12_atomic_integration_tests` | Passed (96/96 variants) | `GUEST_MEMORY_MODEL` |
| **198.13** | Advanced SMP Litmus | MP (DMA+CPU), SB, LB, WRC, and cache maintenance litmus | `stage198_13_advanced_smp_litmus_tests` | Passed (5/5 tests) | `GUEST_MEMORY_MODEL` |
| **198.14** | Snapshot Integration | Full `.wsnap` memory, MMU sysregs, and VirtualDmaDevice restore | `stage198_14_snapshot_memory_tests` | Passed (3/3 tests) | `GUEST_MEMORY_MODEL` |
| **198.15** | Deterministic Replay | 100% bit-exact replay hash repeatability across 5 seeds | `stage198_15_deterministic_replay_tests` | Passed (2/2 tests) | `GUEST_MEMORY_MODEL` |
| **198.16** | Crash Recovery & Boundary | Aborted DMA recovery, corrupted snapshot rejection, RAM edge | `stage198_16_crash_recovery_tests` | Passed (4/4 tests) | `GUEST_DMA` |
| **198.17** | Guest Workload Execution | Executed `guest_memory_dma_cache_workload` Mach-O binary | `stage198_17_guest_workload_tests` | Passed (2/2 tests) | `GUEST_MEMORY_MODEL` |
| **198.18** | Differential Validation | Differential validation of DC ZVA, LSE atomics, DMA, diagnostics | `stage198_18_differential_validation_tests` | Passed (4/4 tests) | `REFERENCE_COMPARISON` |
| **198.19** | Final Regressions & Closeout| Full Release & Debug CTest regressions, manifest & final reports | All 432 CTest suites | Passed (431 pass, 1 skip)| `GUEST_MEMORY_MODEL` |

---

## 2. Test Execution Metrics

- **Total Test Suites in Project:** 432
- **Release Regression Pass Rate:** 431 passed, 1 skipped (`inferno_integration_tests`), 0 failed (100% runnable pass).
- **Total Release Execution Time:** 85.45 seconds.
- **Compiler Warnings:** 0 (`/W4 /WX /permissive-`).
