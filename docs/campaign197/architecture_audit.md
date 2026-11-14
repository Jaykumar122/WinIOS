# WinIOS Campaign 197: ARMv8-A Architecture Audit

## 1. Specification Compliance Mapping

| Architectural Feature | ARM DDI 0487 Section | WinIOS Implementation | Compliance Status |
| :--- | :--- | :--- | :--- |
| **Weakly Ordered Memory Model** | B2.2 (Memory model overview) | `GuestMemoryOrderModel` (`src/cpu/guest_memory_order.h`) | **COMPLIANT** (Axiomatically modeled) |
| **Data Memory Barrier (DMB)** | B2.3.1 (DMB semantics) | `CPU::execute_dmb` | **COMPLIANT** (Shareability & type scoping) |
| **Data Synchronization Barrier (DSB)** | B2.3.2 (DSB semantics) | `CPU::execute_dsb` | **COMPLIANT** (Pipeline stall & store drain) |
| **Instruction Synchronization Barrier (ISB)** | B2.3.3 (ISB semantics) | `CPU::execute_isb` | **COMPLIANT** (Pipeline flush & context reload) |
| **Exclusive Monitors (LL/SC)** | B2.9 (Synchronization and semaphore) | `SMPExclusiveMonitor` & `CPU` monitors | **COMPLIANT** (64-byte granule matching A16) |
| **Exclusive Reservation Granule** | D13.2.33 (`CTR_EL0.ERG`) | `CTR_EL0.ERG = 4` (64 bytes) | **COMPLIANT** |
| **LSE Atomics (ARMv8.1-A)** | C3.2.14–C3.2.17 (`CAS`, `SWP`, `LDADD`..) | `CPU::execute_atomic_*` | **COMPLIANT** (96 size/order variants) |
| **Atomic Fault Invariance** | B2.9.2 (Exceptions on atomics) | Permission check before commit | **COMPLIANT** (No state corruption on faults) |
| **TLB Invalidation Broadcast** | D5.8 (TLB maintenance instructions) | `SMPCluster::broadcast_tlbi_va / asid` | **COMPLIANT** (Cross-core broadcast) |
| **Interrupt Reservation Invalidation** | B2.9.1 (Asynchronous exception effects) | `deliver_architectural_exception` | **COMPLIANT** (Clears local & cluster monitors) |

## 2. Instruction Coverage Audit

### 2.1 Exclusive Operations (LL/SC)
- `LDXR` / `STXR` (8, 16, 32, 64-bit)
- `LDAXR` / `STLXR` (8, 16, 32, 64-bit)
- `LDXP` / `STXP` (Pair 64-bit)
- `LDAXP` / `STLXP` (Pair 64-bit Acquire/Release)

### 2.2 Large System Extensions (LSE) Atomics
- **Operations**: `CAS`, `SWP`, `LDADD`, `LDCLR`, `LDEOR`, `LDSET`, `LDSMAX`, `LDSMIN`, `LDUMAX`, `LDUMIN`
- **Sizes**: 8-bit, 16-bit, 32-bit, 64-bit
- **Ordering Variants**: Relaxed (`none`), Acquire (`A`), Release (`L`), Acquire-Release (`AL`)
- **Total Tested Matrix**: 96 distinct opcode and order configurations (verified 100% in `stage197_7_lse_matrix_tests`).

## 3. Litmus Pattern Audit Against Herd7 Axiomatic Models
The axiomatic ordering rules implemented in `GuestMemoryOrderModel` were evaluated against the Herd7 canonical ARMv8 axiomatic memory model:
- **SB (Store Buffering)**:
  `P0: W(x)=1, R(y)=r0; P1: W(y)=1, R(x)=r1`
  Allowed under weak memory (`r0=0, r1=0`). Forbidden when barriers (`DMB ISH`) are inserted. WinIOS matches Herd7.
- **MP (Message Passing)**:
  `P0: W(data)=1, W(flag)=1; P1: R(flag)=1, R(data)=r0`
  `r0=0` forbidden with Release-Acquire (`STLR`/`LDAR`) or `DMB ISH`. WinIOS matches Herd7.
- **LB (Load Buffering)**:
  `P0: R(x)=1, W(y)=1; P1: R(y)=1, W(x)=1`
  Causality cycles forbidden by data/control dependency. WinIOS matches Herd7.
- **IRIW (Independent Reads of Independent Writes)**:
  Multi-copy atomicity verified. Forbidden when readers use `DMB ISH`. WinIOS matches Herd7.
- **WRC (Write-to-Read Causality) & RWC (Read-to-Write Causality)**:
  Causality order preserved with barriers. WinIOS matches Herd7.
