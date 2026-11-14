# WinIOS Campaign 197: Evidence Classification & Acceptance Matrix

## Evidence Classifications
- `GUEST_EL0`: Unprivileged guest ARM64 userspace execution.
- `GUEST_EL1`: Privileged guest ARM64 kernel mode and exception levels.
- `GUEST_MEMORY_MODEL`: Architectural weak-memory ordering and barrier semantics.
- `GUEST_ATOMIC`: Load-linked/store-conditional and LSE atomic instructions.
- `HOST_MODEL`: Emulation host isolation and runtime boundaries.
- `REFERENCE_COMPARISON`: Comparison against formal specifications (ARM DDI 0487, Herd7).
- `SYNTHETIC_KERNEL`: Synthetic Darwin-oriented kernel services, MMU, and scheduling.

---

## Acceptance Test Matrix (Tests A through O)

| Acceptance Test | Description | Classification | Test Executable / Artifact | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Test A** | DMB semantics distinct from DSB/ISB; shareability and access domains | `GUEST_MEMORY_MODEL` | `stage197_3_barrier_semantics_tests.exe` | **PASSED (100%)** |
| **Test B** | DSB completion behavior & store drain independently verified | `GUEST_MEMORY_MODEL` | `stage197_3_barrier_semantics_tests.exe` | **PASSED (100%)** |
| **Test C** | ISB instruction synchronization & pipeline flush verified | `GUEST_MEMORY_MODEL` | `stage197_3_barrier_semantics_tests.exe` | **PASSED (100%)** |
| **Test D** | Exclusive-monitor reservation granule (64B) & invalidation | `GUEST_ATOMIC` | `stage197_6_exclusive_monitor_tests.exe` | **PASSED (100%)** |
| **Test E** | LSE acquire/release variants (8/16/32/64-bit, 96 total variants) | `GUEST_ATOMIC` | `stage197_7_lse_matrix_tests.exe` | **PASSED (100%)** |
| **Test F** | Atomic accesses do not partially modify memory on faults | `GUEST_ATOMIC`, `GUEST_EL1` | `stage197_8_atomic_fault_tests.exe` | **PASSED (100%)** |
| **Test G** | Guest acquire/release message passing is correct | `GUEST_MEMORY_MODEL` | `stage197_10_litmus_tests.exe` | **PASSED (100%)** |
| **Test H** | Weak-memory litmus outcomes match declared model (SB, MP, LB, IRIW, WRC, RWC) | `GUEST_MEMORY_MODEL`, `REFERENCE_COMPARISON` | `stage197_10_litmus_tests.exe`, `litmus_test_results.json` | **PASSED (100%)** |
| **Test I** | Host synchronization isolated from guest architectural behavior | `HOST_MODEL`, `GUEST_MEMORY_MODEL` | `stage197_12_host_synchronization_isolation_tests.exe` | **PASSED (100%)** |
| **Test J** | Deterministic memory-ordering replay reproducible (FNV-1a hash exact) | `GUEST_MEMORY_MODEL` | `stage197_13_deterministic_replay_tests.exe`, `deterministic_replay_results.json` | **PASSED (100%)** |
| **Test K** | Cross-core TLBI and page reuse preserve memory safety | `GUEST_EL1`, `SYNTHETIC_KERNEL` | `stage197_14_tlbi_page_reuse_tests.exe` | **PASSED (100%)** |
| **Test L** | Interrupts and preemption preserve atomic state and clear reservations | `GUEST_EL1`, `GUEST_ATOMIC` | `stage197_15_interrupt_atomic_tests.exe` | **PASSED (100%)** |
| **Test M** | Independently compiled guest workloads execute successfully | `GUEST_EL0` | `stage197_16_workload_corpus_tests.exe`, Mach-O binaries | **PASSED (100%)** |
| **Test N** | Differential diagnostic tests report exact mismatches | `REFERENCE_COMPARISON` | `stage197_16_workload_corpus_tests.exe` | **PASSED (100%)** |
| **Test O** | 8-core integrated workloads preserve all declared invariants | `SYNTHETIC_KERNEL`, `GUEST_ATOMIC`, `GUEST_MEMORY_MODEL` | `stage197_18_integrated_smp_conformance_tests.exe` | **PASSED (100%)** |
