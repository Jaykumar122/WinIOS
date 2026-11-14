# WinIOS Campaign 197: Architectural Decision Records (ADRs)

## Context & Objectives
Campaign 197 focuses on ARM64 Memory Model Fidelity, Atomic Instruction Correctness, Weak-Memory Simulation & SMP Conformance.
The target research device is iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), running an emulated synthetic ARM64/ARM64e execution environment on a Windows 11 x64 host.

---

### ADR-197-01: Architectural Separation of DMB, DSB, and ISB
* **Status**: Accepted & Implemented
* **Context**: Prior to Campaign 197, memory barriers (`DMB`, `DSB`, `ISB`) were largely treated as generic host compiler fences or no-ops in CPU emulation. ARM DDI 0487 mandates distinct architectural boundaries:
  - `DMB` orders memory accesses before and after the barrier within a specified shareability domain and access type (Reads, Writes, or Any). It does not stall instruction dispatch or wait for non-memory operations.
  - `DSB` ensures that all memory accesses completed before the DSB instruction completes, and halts execution until memory operations, cache/TLB maintenance operations, and store buffers are drained.
  - `ISB` flushes the instruction fetch pipeline and ensures instructions following the ISB are refetched from cache/memory with updated context (system registers, ASID, MMU tables, code modifications).
* **Decision**: Implemented explicit per-barrier dispatch in `CPU::execute_system` calling `execute_dmb(crm)`, `execute_dsb(crm)`, and `execute_isb(crm)`. Added tracking in `GuestMemoryOrderModel` and CPU performance counters (`dmb_count_`, `dsb_count_`, `isb_count_`).
* **Consequences**: Rigorous verification against Acceptance Tests A, B, and C with deterministic test isolation.

---

### ADR-197-02: Configurable Exclusive Monitor Reservation Granule
* **Status**: Accepted & Implemented
* **Context**: ARMv8-A architecture permits exclusive reservation granules between 4 words (16 bytes) and 512 words (2048 bytes). Real Apple Silicon (A16 Bionic) uses a 64-byte cache line reservation granule. Previously, the exclusive monitor checked for exact address equality, which caused false negatives when store invalidations touched adjacent bytes in the same reservation granule.
* **Decision**: Updated `SMPExclusiveMonitor` and `CPU` exclusive monitors to support configurable reservation granule mask (default: 64 bytes, `granule_mask = ~0x3FULL`). Added `CTR_EL0.ERG` reporting matching 64 bytes (`0x4` = 16 words = 64 bytes).
* **Consequences**: Stores anywhere within the 64-byte aligned reservation chunk correctly invalidate reservations across cores. Pass Acceptance Test D.

---

### ADR-197-03: Axiomatic Guest Memory Ordering Simulation Model Decoupled from Host TSO
* **Status**: Accepted & Implemented
* **Context**: The host machine runs on x86-64, which enforces Total Store Order (TSO), preventing Store-Load reordering (SB litmus) and Load-Load reordering. ARMv8-A is a weakly ordered multi-copy atomic architecture. Emulating ARM64 on x86-64 directly would mask missing guest barriers because the host CPU would never reorder memory operations.
* **Decision**: Designed and integrated `GuestMemoryOrderModel` (`src/cpu/guest_memory_order.h`, `src/cpu/guest_memory_order.cpp`) into `winios_core`. It models per-core store buffers, instruction dependency trees, barrier semantics, and axiomatic memory event traces. Litmus tests (SB, MP, LB, IRIW, WRC, RWC) evaluate guest-level barrier correctness independent of host execution.
* **Consequences**: Weak-memory litmus outcomes match declared ARMv8-A semantics. Pass Acceptance Tests H, I, and J.

---

### ADR-197-04: Non-destructive Exception & Translation Fault Handling for LSE Atomics
* **Status**: Accepted & Implemented
* **Context**: ARMv8.1-A Large System Extensions (LSE) instructions (`CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET`, `LDSMAX`, `LDSMIN`, `LDUMAX`, `LDUMIN`) perform atomic read-modify-write operations in a single instruction. If a memory translation or permission fault occurs during the access, no partial state change or destination register mutation must occur.
* **Decision**: Atomic read-modify-write helpers in `CPU` check memory translation permissions for *both* read and write before committing any value to memory or modifying the destination register. If a fault is triggered, the operation aborts cleanly and invokes `deliver_architectural_exception` with exact syndrome registers (FAR_EL1, ESR_EL1).
* **Consequences**: Guarantees all-or-nothing atomicity under page faults and permission faults. Pass Acceptance Test F.

---

### ADR-197-05: Monitor Clearing on Exception and Interrupt Boundaries
* **Status**: Accepted & Implemented
* **Context**: If an asynchronous interrupt or synchronous exception arrives while a CPU holds an exclusive reservation between `LDXR` and `STXR`, the context switch or exception handler could compromise atomic consistency.
* **Decision**: `CPU::deliver_architectural_exception` and `CPU::execute_eret` unconditionally clear both the local exclusive monitor (`exclusive_monitor_valid_ = false`) and the cluster-wide reservation via `smp_cluster_->clear_exclusive_reservation(core_id_)`.
* **Consequences**: Any interrupted `STXR` sequence fails safely with status 1 on retry, preventing race conditions across interrupt handlers. Pass Acceptance Test L.

---

### ADR-197-06: Freestanding Cross-Compiled Guest Workload Corpus with Zero PageZero
* **Status**: Accepted & Implemented
* **Context**: Real Darwin Mach-O executables built with default macOS/iOS toolchains include a 4 GB `__PAGEZERO` segment which fails to load into low-memory kernel test environments.
* **Decision**: Cross-compiled freestanding guest workloads (`guest_atomics_benchmark.S`, `guest_smp_atomic_stress.S`, `guest_smp_message_passing.S`) using Zig 0.17.0 (`-target aarch64-macos -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" "-Wl,-pagezero_size,0"`).
* **Consequences**: Executables load cleanly into guest RAM bounds and validate real ARM64 instruction execution. Pass Acceptance Tests M and N.
