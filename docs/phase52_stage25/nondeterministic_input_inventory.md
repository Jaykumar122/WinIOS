# WinIOS — Phase 52, Stage 25: Nondeterministic Input Inventory

## 1. Classification Methodology
Every source of potential architectural nondeterminism across multi-core virtual execution is audited and classified into 5 formal tiers:
1. **Tier 1: Fully Deterministic & Reconstructible**
2. **Tier 2: Nondeterministic & Recorded in SNDL v2**
3. **Tier 3: Nondeterministic & Missing Recording (SMP Replay Blockers)**
4. **Tier 4: Unsupported & Explicitly Rejected**
5. **Tier 5: Unknown / Unverified**

---

## 2. Complete Subsystem Inventory

| Subsystem | Input Source | Trigger Condition | Classification | Architectural Impact | Recorded Event |
|---|---|---|---|---|---|
| **Scheduler** | Core Selection | Quantum boundary | Tier 2 | Core chosen to run | `CoreSelected` |
| **Scheduler** | Quantum Start | Time-slice alloc | Tier 2 | Instruction budget | `QuantumStart` |
| **Scheduler** | Early Yield | WFI/WFE or trap | Tier 2 | Truncated quantum | `EarlyTruncation` |
| **Scheduler** | Quantum End | Budget expired | Tier 2 | Core yielded | `QuantumEnd` |
| **Lifecycle** | Core Online | PSCI CPU_ON | Tier 2 | Core awakens | `CoreStarted` |
| **Lifecycle** | Core Offline | PSCI CPU_OFF | Tier 2 | Core stops | `CoreStopped` |
| **Lifecycle** | Core Sleep | WFI/WFE | Tier 2 | Core idles | `CoreEnteredWfi` |
| **Interrupts** | Ext Assertion | External peripheral | Tier 2 | Router queue entry | `ExternalInterruptAsserted` |
| **Interrupts** | Routing | Core target select | Tier 2 | Core queue delivery | `InterruptRouted` |
| **Interrupts** | Acceptance | GIC CPU IF ack | Tier 2 | Vector branch | `InterruptAccepted` |
| **Exceptions** | Synchronous | Instruction fault | Tier 1 | Vector branch | Deterministic from state |
| **Timers** | Virtual Time | Instruction step | Tier 1 | Increments 1:1 | Deterministic from state |
| **Timers** | CNTPCT Read | System reg read | Tier 2 | Counter value | `CntpctRead` |
| **Timers** | Expiration | Comparator hit | Tier 2 | PPI interrupt trip | `TimerComparatorExpired` |
| **MMIO** | Device Read | Load from MMIO | Tier 2 | Data reg value | `MmioReadResult` |
| **Entropy** | RNDR / RNDRRS | Random read | Tier 2 | Register value | `EntropyReadResult` |
| **Exclusives** | Store Exclusive | STXR/STLXR | Tier 2 | 0 (success) / 1 (fail) | `StoreExclusiveResult` |
| **Exclusives** | Clear Excl | CLREX | Tier 2 | Clears reservation | `ReservationCleared` |
| **Shared RAM** | Interleaving | Core time-slice | Tier 1 | Interleaved quantum | Deterministic order |
| **DMA** | Async Host DMA| Host buffer inject| Tier 3 | Memory mutation | **MISSING (Blocker)** |
| **Clock** | Host Wall Clock| System time call | Tier 3 | Wall time drift | **MISSING (Blocker)** |
| **Replay** | Multi-Core Replay| Core count > 1 | Tier 4 | Replay invocation | Explicitly Rejected |
