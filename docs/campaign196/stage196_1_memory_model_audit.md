# Stage 196.1 — ARMv8-A Memory Model Audit

## 1. Architectural Memory Model Overview
ARMv8-A specifies a weakly ordered, multi-copy atomic memory model (ARM DDI 0487):
- **Memory Types:**
  - **Normal Memory:** Permits out-of-order execution, speculative reads, write merging, and unaligned accesses (when `SCTLR_EL1.A == 0`). Shareability domain attributes (Non-shareable, Inner Shareable, Outer Shareable) determine cache-coherency scope.
  - **Device Memory:** Enforces strict access order, prevents speculative access, and requires natural alignment regardless of `SCTLR_EL1.A`. Four types: Device-nGnRnE, Device-nGnRE, Device-nGRE, Device-GRE.
- **Ordering Semantics:**
  - **Acquire Semantics (`LDAR`, `LDAXR`, `CAS*A`):** Ensures that memory accesses after the acquire instruction in program order are observed after the acquire load.
  - **Release Semantics (`STLR`, `STLXR`, `CAS*L`):** Ensures that memory accesses before the release instruction in program order are observed before the release store.
  - **Barriers:**
    - `DMB` (Data Memory Barrier): Orders memory accesses across the specified domain (`SY`, `ISH`, `NSH`, `OSH`, `ST`, `LD`).
    - `DSB` (Data Synchronization Barrier): Blocks execution until all memory accesses in scope have completed.
    - `ISB` (Instruction Synchronization Barrier): Flushes pipeline, ensuring subsequent instructions are fetched with updated context (e.g., after page table modification or system register write).

## 2. Emulator Implementation vs. Hardware Reality
| Architectural Feature | Hardware ARM64 (A16 Bionic) | WinIOS Emulation Implementation | Status |
|---|---|---|---|
| Shareability Domains | Hardware cache snoop filters (L1/L2/SLC) | Emulated unified guest address space; SMP cluster shareability | `HOST_MODEL_ONLY` |
| Multi-Copy Atomicity | Hardware guarantees store visibility to all cores simultaneously | Host x86_64 memory model (TSO) provides multi-copy atomicity | `IMPLEMENTED_AND_TESTED` |
| Weak Memory Reordering | Stores may reorder after loads without barrier (SB litmus pattern) | Host x86_64 host processor enforces TSO (Store-Load reordering only); virtual CPU thread execution is sequentially ordered | `HOST_MODEL_ONLY` |
| Natural Alignment on Atomics | `LDXR`/`STXR`/`LDAR`/`STLR` require natural alignment | Checked in `cpu.cpp` (`address & (width - 1) == 0`); raises Alignment Fault | `IMPLEMENTED_AND_TESTED` |
| Reservation Granule | 64 bytes (Apple Silicon / standard ARM64) | 64 bytes (`kGranuleSize = 64`) in `SmpExclusiveMonitorManager` | `IMPLEMENTED_AND_TESTED` |
| Local & Global Monitors | Core-private tagged address + cluster snoop invalidation | Local `exclusive_monitor_` + thread-safe `SmpExclusiveMonitorManager` | `IMPLEMENTED_AND_TESTED` |
| Barriers (`DMB`, `DSB`, `ISB`) | Pipeline stall & interconnect drain per domain/types | Sequentially consistent thread fences (`std::atomic_thread_fence(memory_order_seq_cst)`) + instruction decode | `PARTIALLY_IMPLEMENTED` (CRm options need auditing) |
| LSE Atomics (`CAS`, `SWP`, `LDADD`, etc.) | ARMv8.1-A single-instruction atomic RMW | CAS partially implemented; `SWP`/`LDADD`/`LDCLR`/`LDSET`/`LDEOR` to expand | `PARTIALLY_IMPLEMENTED` |

## 3. Discrepancies and Audit Findings
1. **Barrier CRm Options:** Currently, `DMB`, `DSB`, `ISB` only check `rt == 31` and ignore the `CRm` option bits (`crm`). Valid ARM64 encodings have specific `CRm` values (e.g. `0b1011` for `ISH`, `0b1111` for `SY`). Stage 196.5 will validate and classify these options.
2. **Atomic Expansion:** Only single-register `CAS` is currently decoded. Real Darwin and compiled ARM64 code (such as libSystem and Mach-O binaries) utilize `SWP`, `LDADD`, `LDCLR`, `LDSET`, and `LDEOR`. Stage 196.3 will implement these encodings.
3. **Litmus Test Distinction:** On x86_64 host architectures, hardware does not permit Load-Load or Store-Store reordering. Therefore, weak litmus test outcomes (like store reordering in message passing without release) will not be observed naturally in a single instruction emulator on x86_64 without intentional randomized interleaving. This is documented explicitly as an architectural emulator property (`HOST_MODEL`).
