# Stage 195.17 — Long-Running Synthetic Workload Corpus & Deterministic Replay (Mandatory Test I)

## Subsystem Overview
- **Component**: End-to-End Synthetic Kernel Execution Pipeline & Deterministic Replay Validation
- **Architectural Classification**: `GUEST_EL1` / `SYNTHETIC_KERNEL` / `GUEST_SMP` / `GUEST_PREEMPTION`
- **Device Research Target**: iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), Apple A16 Bionic (`T8120`)

---

## 1. Objectives & Mandatory Acceptance Criteria
Stage 195.17 fulfills **Mandatory Test I: Deterministic replay of long-running synthetic kernel workloads**:
1. **Integrated Subsystem Pipeline**:
   - Asynchronous timer preemption with complete 896-byte `ExceptionFrame` push and pop on the guest stack.
   - General-purpose register (GPR) state preservation (`X0–X30`).
   - Floating-point / SIMD vector register state preservation (`Q0–Q31`, `FPCR`, `FPSR`).
   - Processor status register (`PSTATE`, `NZCV`, `DAIF`) isolation.
   - Dual-core SMP dispatch and scheduling with `SmpScheduler`.
   - Mid-workload task migration from Core 0 to Core 1 via affinity re-assignment.
   - Inter-core lock synchronization via `KernelSpinlock` (`try_acquire_host` / `release_host`).
   - On-demand virtual memory translation and EL1 page fault resolution via `DemandPagingManager`.
2. **Deterministic Replay Guarantee**:
   - The integrated scenario is executed twice (`Run 1` and `Run 2`) from identical initial guest machine state.
   - Metrics tracked and verified for exact equality:
     - Retired instruction count
     - Timer preemption count
     - Context switch count
     - Demand page faults resolved
     - Spinlock acquire operations
     - Full 64-bit FNV-1a cryptographic state hash over all registers (`X0–X30`, `Q0–Q31`, `PC`, `SP`, `FAR`, `ESR`, `DAIF`), hardware state, synchronization objects, and memory stacks.

---

## 2. Test Execution & Comparative Replay Results
Executable: `stage195_17_long_running_workload_tests.exe`

| Metric | Run 1 Result | Run 2 Result | Replay Conformance | Status |
|:---|:---:|:---:|:---:|:---:|
| **Retired Instructions** | 641 | 641 | 100% Identical | **MATCH** |
| **Preemptions Handled** | 4 | 4 | 100% Identical | **MATCH** |
| **Context Switches** | 5 | 5 | 100% Identical | **MATCH** |
| **Page Faults Resolved** | 1 | 1 | 100% Identical | **MATCH** |
| **Spinlock Operations** | 30 | 30 | 100% Identical | **MATCH** |
| **Machine State Checksum** | `0x3d3ff8aef8b61e8f` | `0x3d3ff8aef8b61e8f` | Bit-for-bit Identical | **MATCH** |

---

## 3. Compliance and Sign-Off
- Zero compiler warnings under MSVC `/W4 /WX /permissive-`.
- 100% of deterministic replay tests passed.
- Mandatory Acceptance Test I fully satisfied.
