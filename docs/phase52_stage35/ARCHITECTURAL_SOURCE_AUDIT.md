# WinIOS Phase 52 Stage 35: Architectural Source Audit

**Project:** WinIOS AArch64 Emulation & Deterministic Replay  
**Phase:** 52, Stage 35  
**Pinned Inferno SHA:** `cc4302a99167abec69b714cfd00c38caece7e7de`  
**Date:** November 6, 2026  

---

## 1. Executive Summary & Audit Methodology

This audit independently examines the WinIOS codebase (`src/cpu/`, `src/memory/`, `src/device/`, `src/reference/`, `src/snapshot/`, `src/sndl/`, `src/replay/`) to establish an empirical inventory of architectural state, translation mechanisms, exception routing, interrupt handling, and memory ordering.

Every supported architectural operation has been audited against its concrete source code to distinguish:
1. **Implemented and Tested:** Fully realized in code and covered by automated regression tests.
2. **Implemented but Incompletely Tested:** Fully present in code, but with gaps in boundary/differential coverage.
3. **Partially Implemented:** Architectural subset implemented (e.g. 4KB granule only, specific exception classes).
4. **Explicitly Unsupported:** Architectural paths that trap, fault, or abort with explicit diagnostics.
5. **Incorrect or Inconsistent:** Behaviors that diverge from architectural specifications or omit state from invariants.

---

## 2. Findings Register (Stages 34 & 35)

| Finding ID | Severity | Source Location | Description | Existing Coverage | Resolution in Stage 35 | Verification Status |
|---|---|---|---|---|---|---|
| **AUD-34-01** | High | `src/cpu/smp_scheduler.cpp:286` | Canonical cluster hash omits device bus state (Section 0x18), exclusive monitor reservations, and router pending queues | Omitted in `compute_canonical_multicore_hash` | Implement `compute_extended_cluster_hash()` incorporating device bus, monitors, and router state | Verified in `gic_memory_ordering_tests` |
| **AUD-34-02** | Medium | `src/device/dma_payload_store.cpp:383` | Unbounded `resize(length)` in companion reader could trigger `std::bad_alloc` on adversarial length | Checked for EOF but not maximum length bound | Add sanity upper bound `kMaxSupportedPayloadSize = 64 * 1024 * 1024` | Verified in fuzzing & payload tests |
| **AUD-34-03** | Medium | `src/cpu/smp_exclusive_monitor.cpp:67` | Potential 64-bit integer wraparound in `address + size - 1` when address approaches `UINT64_MAX` | Checked range within bounds | Add saturating arithmetic / check `(address > UINT64_MAX - size)` | Verified in monitor edge tests |
| **AUD-34-04** | Medium | `src/device/recording_recovery.cpp:142` | Potential committed RAM loss if trailing uncommitted DMA is trimmed without quantum rollback | Quantum rollback implemented in Stage 33 | Enforce strict invariant that trailing orphan DMA trimming rolls back the enclosing quantum | Verified in `independent_recording_recovery_tests` |
| **AUD-34-05** | Low | `tests/unit/dma_recording_replay_tests.cpp` | Absence of independent reference oracle in existing tests (record vs replay only) | Stage 34 created `IndependentReferenceModel` | Extend reference model to cover MMU, exceptions, GIC, and litmus memory ordering | Verified in `mmu_conformance_tests` |
| **AUD-34-06** | Low | `src/cpu/smp_interrupt_router.h` | 8-core bitmask limit (`uint8_t target_cores_mask`) in SPI interrupt routing | 1, 2, 4, 8 core topologies supported | Retain strict capability gating rejecting topologies > 8 cores | Verified in SMP topology tests |
| **AUD-35-01** | Medium | `src/cpu/mmu.cpp:258` | TBI (Top Byte Ignore) evaluation relies on TCR bitmask; 16KB and 64KB translation granules unsupported | Only 4KB granule implemented | Document explicit limitation: 4KB granule only; reject 16K/64K in TCR | Verified in `mmu_conformance_tests` |
| **AUD-35-02** | Medium | `src/cpu/cpu.cpp:7320` | ERET illegal state handling sets `il_ = true` but allows PC update before next step traps | PSTATE.IL traps on next fetch | Verify PSTATE.IL causes immediate Illegal Execution State synchronous fault on next instruction | Verified in `architectural_state_integrity_tests` |
| **AUD-35-03** | Low | `src/cpu/mmu.cpp:171` | TLB invalidation by VA masks lower 12 bits, but does not support variable ASID width (8-bit vs 16-bit TCR.AS) | Uses 16-bit ASID | Audit ASID isolation; verify global vs non-global eviction | Verified in `mmu_conformance_tests` |
| **AUD-35-04** | Low | `src/cpu/gicv3.cpp:73` | GICv3 evaluate tie-breaking when priority values match relies on iterator sequence | Evaluated in core order | Enforce deterministic tie-breaking: when priorities match, lower INTID takes precedence | Verified in `gic_memory_ordering_tests` |
| **AUD-35-05** | Medium | `src/cpu/cpu.cpp:7631` | Barriers (`DMB`, `DSB`, `ISB`) execute host fences (`std::atomic_thread_fence`) but do not model out-of-order store buffers | Single-host-thread scheduler | Document that memory model is sequentially consistent by virtue of serialized deterministic scheduling | Verified in `gic_memory_ordering_tests` |
| **AUD-35-06** | Low | `src/cpu/cpu.cpp:5114` | PC alignment check tests `instruction_address & 0x3u`, but does not verify SP alignment on SP-relative loads/stores | SCTLR_EL1.SA0 / SA1 | Implement SP alignment check when SA bit is set; raise SPAlignment fault | Verified in `architectural_state_integrity_tests` |

---

## 3. Comprehensive Audit of 20 Architectural Subsystems

### 1. CPU Register File
- **Source:** `src/cpu/cpu.h`, `src/cpu/cpu.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** 31 64-bit general-purpose registers (X0–X30), separate Zero Register (XZR/WZR in instruction decodings), 32 128-bit SIMD/Vector registers (V0–V31), FPCR, FPSR. 32-bit W-register writes strictly zero-extend the upper 32 bits into the corresponding X register.

### 2. AArch64 Instruction Execution
- **Source:** `src/cpu/cpu.cpp` (lines 5100–9580)
- **Implementation Status:** **Partially Implemented (Bounded Research Subset)**
- **Details:** Implements standard integer ALU (ADD, SUB, AND, ORR, EOR, shifted register forms), load/store (LDR, STR, LDP, STP, byte/half/word/double), load/store exclusive (LDXR, STXR, LDAXR, STLXR, CLREX), branches (B, B.cond, BL, BR, BLR, RET), system register access (MRS, MSR), barriers (DMB, DSB, ISB), and exception instructions (SVC, HVC, SMC, ERET). Unsupported instructions trigger explicit `SynchronousException` (Undefined Instruction).

### 3. NZCV Condition Flags
- **Source:** `src/cpu/cpu.h`, `src/cpu/cpu.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** 4-bit status field (N: bit 3, Z: bit 2, C: bit 1, V: bit 0). Condition code testing (EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL) conforms to Armv8-A specifications. Arithmetic instructions with 'S' suffix (ADDS, SUBS, CMP, CMN, ANDS, TST) update NZCV accurately.

### 4. CPU Exception State
- **Source:** `src/cpu/cpu.h`, `src/cpu/synchronous_exception.h`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Dedicated exception registers for EL1, EL2, and EL3: ELR (Exception Link Register), SPSR (Saved Program Status Register), ESR (Exception Syndrome Register), FAR (Fault Address Register), VBAR (Vector Base Address Register).

### 5. Exception-Level Transitions
- **Source:** `src/cpu/cpu.cpp` (`raise_synchronous_exception`, `raise_el2_exception`, `raise_el3_exception`, ERET)
- **Implementation Status:** **Implemented and Tested**
- **Details:** Transitions from EL0 to EL1, EL1 to EL1, EL0/EL1 to EL2, and EL0/EL1/EL2 to EL3. Vector offsets:
  - Synchronous from lower EL (AArch64): `+0x400`
  - Synchronous from current EL with SP_ELx: `+0x200`
  - Synchronous from current EL with SP_EL0: `+0x000`
  - IRQ/FIQ from lower EL: `+0x480` / `+0x500`
  - IRQ/FIQ from current EL (SP_ELx): `+0x280` / `+0x300`
  - IRQ/FIQ from current EL (SP_EL0): `+0x080` / `+0x100`

### 6. MMU Translation
- **Source:** `src/cpu/mmu.h`, `src/cpu/mmu.cpp`
- **Implementation Status:** **Partially Implemented (4KB Granule Only)**
- **Details:** Supports Stage-1 (EL1&0) and Stage-2 (EL2). Evaluates TCR_EL1 (T0SZ, T1SZ, EPD0, EPD1, TBI0, TBI1, A1 ASID selection), TTBR0_EL1, TTBR1_EL1, MAIR_EL1.
- **Explicit Limitation:** 16KB and 64KB translation granules are explicitly unsupported (only 4KB granule is implemented).

### 7. Page-Table Walking
- **Source:** `src/cpu/mmu.cpp` (`walk_stage1`, `walk_stage2`)
- **Implementation Status:** **Implemented and Tested**
- **Details:** Hardware walk emulation across 4 levels (Level 0, 1, 2, 3). Supports 1GB Level 1 blocks, 2MB Level 2 blocks, and 4KB Level 3 pages. Evaluates descriptor validity, reserved bits [51:48], table descriptors, and output address construction.

### 8. Memory Access Permissions
- **Source:** `src/cpu/mmu.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Evaluates AP[2:1] bits, APTable hierarchical bits, UXN/PXN execution permissions, and WXN (Write-implies-XN) from SCTLR_EL1. Translation and permission faults populate exact 6-bit FSC codes.

### 9. TLB Implementation
- **Source:** `src/cpu/mmu.h`, `src/cpu/mmu.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Software TLB cache (64 entries) with ASID/VMID tagging, nG (non-global) bit filtering, and full invalidation primitives (`TLBI VMALLE1`, `TLBI VAE1`, `TLBI ASIDE1`, `TLBI VAAE1`, `TLBI IPAS2E1`).

### 10. Physical Memory Access
- **Source:** `src/memory/memory.h`, `src/memory/memory.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Flat contiguous host allocation with strict boundary checking. Unaligned access handling conforms to architecture requirements.

### 11. Shared Memory
- **Source:** `src/cpu/smp_shared_memory.h`, `src/cpu/smp_shared_memory.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Provides atomic and non-atomic access to unified physical guest memory across all virtual SMP cores.

### 12. Exclusive Monitor
- **Source:** `src/cpu/smp_exclusive_monitor.h`, `src/cpu/smp_exclusive_monitor.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Granular reservation tracking (64-byte cache line granule). Cross-core stores invalidate matching reservations. Evaluated under high-concurrency contention in Stages 28–34.

### 13. Barrier Execution
- **Source:** `src/cpu/cpu.cpp` (`DMB`, `DSB`, `ISB`)
- **Implementation Status:** **Implemented (Sequentially Consistent Environment)**
- **Details:** Emits `std::atomic_thread_fence(std::memory_order_seq_cst)`. Because the emulator uses a single-host-thread deterministic round-robin scheduler, memory operations are serialized by construction.

### 14. GICv3 Distributor & Redistributor
- **Source:** `src/cpu/gicv3.h`, `src/cpu/gicv3.cpp`
- **Implementation Status:** **Partially Implemented (Bounded Subset)**
- **Details:** GICD base at `0x08000000`, GICR base at `0x080A0000`. Models 16 SGIs (INTID 0–15), 16 PPIs (INTID 16–31), and 96 SPIs (INTID 32–127). Evaluates Priority Mask Register (ICC_PMR_EL1), Binary Point Registers (ICC_BPR0/1), and IAR/EOIR acknowledgement.
- **Explicit Limitation:** LPIs (Locality-specific Peripheral Interrupts) and ITS (Interrupt Translation Service) are explicitly unsupported.

### 15. Virtual Timers
- **Source:** `src/cpu/virtual_time.h`, `src/cpu/virtual_time.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Monotonic virtual physical counter (`CNTPCT_EL0`) and virtual counter (`CNTVCT_EL0`). Timer comparison against CVAL/TVAL asserts PPI 27 (CNTV), PPI 30 (CNTP), or PPI 26 (CNTHP). Host wall-clock access is strictly decoupled.

### 16. SMP Scheduler
- **Source:** `src/cpu/smp_scheduler.h`, `src/cpu/smp_scheduler.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Single-host-thread round-robin scheduling across 1, 2, 4, and 8 virtual cores. Fixed instruction quantum budget $Q$ with deterministic truncation triggers (WFI, WFE, HLT, Exception).

### 17. SNDL Architectural Event Recording
- **Source:** `src/sndl/sndl_v2_recorder.h`, `src/sndl/sndl_v2_recorder.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** 80-byte binary event records capturing CoreSelected, QuantumStart, QuantumEnd, InstructionRetired, BranchTaken, ExceptionEntered, ExceptionReturned, InterruptDelivered, and DMA events (70–78).

### 18. Replay Event Injection
- **Source:** `src/replay/smp_replay_coordinator.h`, `src/replay/smp_replay_cursor.h`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Synchronizes replay execution against recorded event stream, validating per-core retirement counters and global sequence invariants at every quantum boundary.

### 19. Canonical Cluster Hashing
- **Source:** `src/cpu/smp_scheduler.cpp`, `src/cpu/smp_core_state.cpp`
- **Implementation Status:** **Audited and Hardened (Stage 35)**
- **Details:** Incorporates core registers, PSTATE, system registers, memory CRC, exclusive reservations, interrupt router queues, and device bus states via `compute_extended_cluster_hash()`.

### 20. Snapshot Serialization & Restoration
- **Source:** `src/snapshot/smp_snapshot_manager.h`, `src/snapshot/smp_snapshot_format.cpp`
- **Implementation Status:** **Implemented and Tested**
- **Details:** Multi-section `.wsnap v2` format (Core state 0x10, Memory 0x11, Scheduler 0x12, GIC 0x13, Timer 0x14, DeviceBus 0x18). Proved bit-for-bit parity across continuation boundaries in Stage 34.
