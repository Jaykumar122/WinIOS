# Stage 197.1 — ARMv8-A Memory Model Specification Audit

## 1. Specification Reference
This audit evaluates the WinIOS memory system against the **ARM Architecture Reference Manual ARMv8, for ARMv8-A architecture profile (ARM DDI 0487)**, specifically:
- **Section B2.1 & B2.2**: The ARMv8 Application Level Memory Model (Weak memory ordering, single-copy atomicity, coherence).
- **Section B2.2.2**: Acquire and Release Semantics (`LDAR`, `STLR`, `LDAXR`, `STLXR`).
- **Section B2.3**: Memory Barriers (`DMB`, `DSB`, `ISB`) and Shareability Domains.
- **Section B2.7**: Memory Types and Attributes (Normal vs Device memory, cacheability, shareability).
- **Section B2.9**: Synchronization and Exclusive Accesses (`LDXR`, `STXR`, `CLREX`, `ERET`).
- **Section C3.2 & C6.2**: ARMv8.1-A Large System Extensions (LSE) Atomics (`CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET`).

---

## 2. Specification-to-Implementation Matrix

Every architectural behavior is classified strictly into one of five categories:
1. `Implemented and tested`: Conforming architectural logic with direct test coverage.
2. `Partially implemented`: Functional for common subset, incomplete for edge cases or full type matrix.
3. `Host-model approximation`: Relies on Windows x64 host guarantees (TSO, C++ `std::atomic_thread_fence`, mutexes) rather than guest architectural modeling.
4. `Unsupported`: Not implemented in the guest engine.
5. `Not yet tested`: Logic exists in source code but lacks dedicated unit/litmus verification.

| Architectural Feature | ARM DDI 0487 Section | Current WinIOS Implementation | Classification | Gap / Action Required in Campaign 197 |
|:---|:---:|:---|:---:|:---|
| **Normal Memory Ordering** | B2.1, B2.2 | Direct reads/writes to host `Memory` buffer. Ordering is preserved by x86-64 TSO hardware. | `Host-model approximation` | Build guest-side memory-ordering model with pending write buffers to simulate weakly ordered outcomes. |
| **Acquire Semantics (`LDAR`, `LDAXR`)** | B2.2.2 | Decoded in `cpu.cpp`; calls `std::atomic_thread_fence(std::memory_order_acquire)`. | `Host-model approximation` | Decouple from host fence; track guest load-acquire ordering barriers in guest memory model. |
| **Release Semantics (`STLR`, `STLXR`)** | B2.2.2 | Decoded in `cpu.cpp`; calls `std::atomic_thread_fence(std::memory_order_release)`. | `Host-model approximation` | Track store-release visibility order in guest memory model. |
| **Exclusive Load/Store (`LDXR`, `STXR`)** | B2.9 | `CPU::exclusive_monitor_` records reservation; `STXR` verifies validity; `CLREX` clears. | `Implemented and tested` | Verify configurable reservation granule and edge-case interference. |
| **Exclusive Pair (`LDXP`, `STXP`)** | B2.9 | 16-byte paired atomic accesses supported and tested. | `Implemented and tested` | Maintain regression coverage. |
| **ERET Monitor Clearance** | B2.9.5 | `CPU::execute_eret` invokes `clear_exclusive_monitor()`. | `Implemented and tested` | Fully verified across exception transitions. |
| **Reservation Granule Tracking** | B2.9.3 | Handled range-based in `CPU` and 64-byte in `SmpExclusiveMonitorManager`. | `Partially implemented` | Make reservation granule explicitly configurable and decoupled from cache line size. |
| **LSE Atomics (32-bit & 64-bit)** | C6.2 | `CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET` implemented with host atomics. | `Implemented and tested` | Expand to full matrix in Stage 197.7. |
| **LSE Atomics (8-bit & 16-bit)** | C6.2 | Byte and halfword size fields (`size=00`, `size=01`) partially handled in decode. | `Partially implemented` | Implement full 8-bit and 16-bit LSE variants (`CASB`, `CASH`, `SWPB`, `SWPH`, `LDADDB`, etc.). |
| **LSE Atomic Fault Safety** | B2.2, D13.2 | If atomic access crosses page boundary or faults, memory must not be partially modified. | `Partially implemented` | Validate all-or-nothing atomic writes across translation/permission faults. |
| **Data Memory Barrier (`DMB`)** | B2.3 | Decodes `crm` option, issues C++ atomic thread fence. | `Host-model approximation` | Model guest DMB constraint: enforce ordering between prior and subsequent guest memory events. |
| **Data Synchronization Barrier (`DSB`)** | B2.3 | Issues `std::atomic_thread_fence(std::memory_order_seq_cst)`. | `Host-model approximation` | Implement DSB completion semantics: wait for all prior explicit memory transactions to complete. |
| **Instruction Synchronization Barrier (`ISB`)** | B2.3 | Incorrectly calls data fence `std::atomic_thread_fence(std::memory_order_seq_cst)`. | `Host-model approximation` | Remove data fence; implement instruction prefetch flush and sysreg context synchronization. |
| **Shareability Domains** | B2.3.2 | Barrier options distinguish Inner/Outer/System, but all map to host-wide fences. | `Host-model approximation` | Model domain filtering in multi-core SMP guest cluster. |
| **Device Memory Ordering** | B2.7 | Device accesses bypass unaligned access; marked non-cacheable in Stage-1 MMU. | `Implemented and tested` | Verify ordering interactions with normal memory. |

---

## 3. Critical Findings for Campaign 197
1. **Host TSO Masking**: Because the Windows host is an x86-64 machine enforcing Total Store Order (TSO), weak memory reorderings permitted by ARMv8-A (such as Load-Load or Store-Store reordering without barriers) never occur naturally on host hardware. Passing litmus tests on x86 does not prove guest weak-memory fidelity unless a guest memory-ordering model is explicitly exercised.
2. **ISB Semantics Defect**: Modeling `ISB` as `std::atomic_thread_fence(seq_cst)` is architecturally inaccurate. `ISB` flushes instruction pipelines and synchronizes instruction fetch with system registers; it has no data memory ordering guarantees.
3. **DSB vs DMB Distinction**: `DSB` ensures *completion* of prior memory accesses before subsequent instructions execute, whereas `DMB` only ensures *observation order* between memory accesses.
4. **LSE Family Completeness**: Need explicit coverage for 8-bit (`B`) and 16-bit (`H`) widths across all LSE operations, plus atomicity guarantees when an access faults on the second byte or page boundary.
