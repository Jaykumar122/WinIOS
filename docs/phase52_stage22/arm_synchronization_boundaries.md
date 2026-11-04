# ARMv8 Synchronization Boundaries & Barrier Semantics

## 1. Architectural Scope

In ARMv8-A architecture (ARM DDI 0487), memory barriers and synchronization instructions define ordering constraints across execution pipelines, memory interfaces, and processor cores.

This document formalizes the synchronization boundary semantics supported in WinIOS and their integration with the deterministic SMP scheduler.

---

## 2. Synchronization Boundary Table

| Instruction | Architectural Mnemonic | Encoding | Subsystem Scope | WinIOS Semantics in SMP Foundation |
|---|---|---|---|---|
| **Data Memory Barrier** | `DMB` | `0xD5033xBF` | Memory Interface | Ensures all explicit memory accesses appearing in program order before the DMB are observed before any explicit memory accesses appearing after the DMB. Truncates quantum in fine-grained scheduler mode. |
| **Data Synchronization Barrier** | `DSB` | `0xD5033x9F` | Memory & Pipeline | Completes all explicit memory transfers, cache operations, and TLB invalidations before subsequent instructions can execute. Truncates current scheduling quantum. |
| **Instruction Synchronization Barrier** | `ISB` | `0xD5033FDF` | Context & Pipeline | Context-synchronizing event. Flushes the instruction pipeline and refetches subsequent instructions from cache/memory. Truncates current scheduling quantum. |
| **Wait For Interrupt** | `WFI` | `0xD503207F` | Core Lifecycle | Evaluates pending unmasked interrupts. If no interrupt is pending, places PE in quiescent `WaitingForInterrupt` state and yields quantum immediately (`CoreWaitingForInterrupt`). |
| **Wait For Event** | `WFE` | `0xD503205F` | Core Lifecycle | Evaluates local event register. If event register is clear, places PE in quiescent `WaitingForEvent` state and yields quantum immediately (`CoreWaitingForEvent`). |
| **Send Event** | `SEV` | `0xD503209F` | Inter-Core Signaling | Asserts the local event register on the calling core and broadcasts event assertion to all virtual cores in the inner shareable domain. |
| **Send Event Local** | `SEVL` | `0xD50320BF` | Local Core Signaling | Asserts the local event register on the calling core only. |
| **Clear Exclusive** | `CLREX` | `0xD5033F5F` | Exclusive Monitor | Clears any active exclusive reservation held by the calling core in `SmpExclusiveMonitorManager`. |
| **Halt Execution** | `HLT` | `0xD440xxxx` | Debug / Core State | Halts execution of the calling core. Transitions core to `CoreLifecycleState::Halted` and immediately truncates quantum (`CoreHalted`). |
| **Software Generated Interrupt** | `SGI` | `MSR ICC_SGI1R_EL1` | Cross-Core IPI | Issues inter-processor interrupt to target core(s). Enqueues deterministic request in `SmpInterruptRouter`. |

---

## 3. Quantum Truncation at Synchronization Boundaries

Because virtual cores execute interleaved on a single host thread, instructions that synchronize with other cores (such as barriers, WFI/WFE, or IPI generation) serve as natural quantum truncation boundaries.

When a virtual core hits a synchronization boundary:
1. Architectural effects of the instruction commit (e.g. state transition, event broadcast, or barrier completion).
2. The current scheduling slice terminates with the appropriate `QuantumTruncationReason`.
3. The scheduler advances to the next runnable core.

This guarantees that cross-core observable effects take place with minimal instruction latency, preventing cores from spinning through large instruction quotas while waiting on a peer core.
