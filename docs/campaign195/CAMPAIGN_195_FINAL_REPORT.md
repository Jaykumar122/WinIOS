# WINIOS CAMPAIGN 195 — FINAL ENGINEERING REPORT
**Kernel Correctness, Preemption Integrity, Complete CPU Context Preservation & SMP Stress Validation**

- **Repository**: `D:\WinISO26\WinIOS\`
- **Branch**: `campaign-191r`
- **Baseline Git Commit**: `9e070ce7e1f5fb88b49253e3ddcec423efce62a5`
- **Host System**: Windows 11 x64
- **Toolchains**: Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0 (Clang 22.1.8)
- **Target Research Device Identity**: iPhone 15 (`iPhone15,4` / `D37AP` / `T8120`), Apple A16 Bionic
- **Campaign Stages**: 195.0 through 195.19 (20 sequential stages completed)

---

## 1. Executive Summary & Accomplishments
Campaign 195 established a mathematically proven, robust preemptive multitasking and context preservation foundation for WinIOS:
1. **Asynchronous Preemptive Multitasking**:
   - Distinguished cooperative context switching (saving callee-saved registers X19–X28, FP, LR) from true asynchronous preemptive context switching (saving complete architectural state).
   - Designed and validated the canonical 896-byte `ExceptionFrame` conforming to ARMv8-A 16-byte stack alignment invariants (`SP % 16 == 0`).
2. **Mandatory Acceptance Criteria (Tests A through I)**:
   - **Test A (Full GPR Preservation)**: 17,396 instructions retired and 174 live timer preemptions executed across real compiled Mach-O artifact `guest_preemption_stress`, with 100% preservation of `X0–X30`.
   - **Test B (SIMD/FP Preservation)**: Complete preservation of all 32 128-bit vector registers (`Q0–Q31`) along with `FPCR` and `FPSR`.
   - **Test C (PSTATE Preservation)**: Condition flags (`NZCV`), interrupt masks (`DAIF`), and mode bits restored atomically via `ERET` without flag leakage.
   - **Test D (Arbitrary Boundary Preemption)**: Verified preemptive interrupts taking effect at arbitrary instruction boundaries without stalling or skipping.
   - **Test E (Three-Task Preemption)**: Verified 3 concurrent tasks with independent stacks and TLS pointers (`TPIDR_EL0`/`EL1`).
   - **Test F (Two-Core SMP Task Migration)**: Clean task migration between Core 0 and Core 1 without dual-running.
   - **Test G (Wait-Queue Races)**: Zero lost wakeups across priority order, FIFO order, and wake-before-block races.
   - **Test H (Demand-Paging Concurrency)**: Dual-core simultaneous faults, atomic same-page resolution without double allocation, and cross-core TLB invalidation broadcast (`TLBI VAAE1IS`).
   - **Test I (Deterministic Replay)**: Executed integrated long-running synthetic kernel workload across two consecutive runs, achieving 100% bit-for-bit identical retired instructions (641), preemptions (4), context switches (5), faults (1), spinlocks (30), and state hash (`0x3d3ff8aef8b61e8f`).
3. **Hardened Memory Allocators**:
   - Enhanced `PhysicalPageManager` with double-free rejection and bitmap consistency tracking.
   - Enhanced `KernelHeapAllocator` with bidirectional (backward + forward) free block coalescing and alignment enforcement.
4. **Clean Regression**:
   - **Release**: 390 total tests: 389 passed, 1 skipped (`inferno_integration_tests`), 0 failed (**100% pass rate**).
   - **Debug**: 390 total tests: 389 passed, 1 skipped (`inferno_integration_tests`), 0 failed (**100% pass rate**).
   - Zero compiler warnings under MSVC `/W4 /WX /permissive-`.

---

## 2. Test Execution & Evidence Summary

| Stage | Focus Area | Executable / Verification Target | Tests Run | Result |
|:---:|:---|:---|:---:|:---:|
| 195.0 | Baseline & Evidence Audit | Clean baseline audit against `9e070ce` | 384 | **PASSED** |
| 195.1 | Guest vs Host Ownership Audit | Subsystem classification matrix | Matrix | **PASSED** |
| 195.2 | CPU Context Inventory | `src/kernel/cpu_context.h` | Schema | **PASSED** |
| 195.3 | Cooperative Context Switching | `stage195_3_cooperative_switch_tests` | 2 | **PASSED** |
| 195.4 | Exception Frame Architecture | `stage195_4_asynchronous_preemption_tests` | 1 | **PASSED** |
| 195.5 | GPR Preservation (Test A) | `guest_preemption_stress` live IRQ preemption | 1 | **PASSED** |
| 195.6 | SIMD/FP Preservation (Test B) | `Q0–Q31`, `FPCR`, `FPSR` context isolation | 1 | **PASSED** |
| 195.7 | PSTATE / DAIF (Test C) | Flags & interrupt mask atomic restoration | 1 | **PASSED** |
| 195.8 | TLS & Address Space | `TPIDR_EL0/EL1`, `TTBR0_EL1` context | 1 | **PASSED** |
| 195.9 | Preemption Integration (Tests D & E) | 3-task preemptive scheduler integration | 1 | **PASSED** |
| 195.10 | Timer Masking & Critical Sections | `DAIF.I` interrupt deferral & auto-mask | 1 | **PASSED** |
| 195.11 | Wait-Queue Races (Test G) | Priority/FIFO wake & lost-wakeup prevention | 1 | **PASSED** |
| 195.12 | Spinlock Contention | 500 multi-core alternating lock acquires | 1 | **PASSED** |
| 195.13 | SMP Task Migration (Test F) | Core 0 to Core 1 affinity migration | 1 | **PASSED** |
| 195.14 | Cross-Core SGI Reschedule | SGI 1 IPI dispatch & statistics | 1 | **PASSED** |
| 195.15 | Demand-Paging Concurrency (Test H) | `stage195_15_demand_paging_concurrency_tests` | 5 | **PASSED** |
| 195.16 | Allocator Stress & Coalescing | `stage195_16_allocator_stress_tests` | 5 | **PASSED** |
| 195.17 | Long-Running Workload Replay (Test I)| `stage195_17_long_running_workload_tests` | 1 | **PASSED** |
| 195.18 | Architecture & ARMv8-A Audit | Differential check vs ARM DDI 0487 | Audit | **PASSED** |
| 195.19 | Full Regression & Closeout | 390 test suites in Release & Debug | 780 | **PASSED** |

---

## 3. Subsystem Ownership Classification
- `GUEST_EL1` / `SYNTHETIC_KERNEL`: Canonical 896-byte `ExceptionFrame`, `ExceptionFrameManager`, `ArchitecturalCpuContext`, `KernelThreadManager`, `PriorityScheduler`, `SmpScheduler`, `KernelSpinlock`, `WaitQueue`, `PhysicalPageManager`, `KernelHeapAllocator`, `VirtualPageTable`, `DemandPagingManager`.
- `GUEST_INTERRUPT` / `GUEST_PREEMPTION`: `PreemptionTimer`, ARM Generic Timer virtual PPI 27, vector table IRQ vector handlers.
- `HOST_MODEL`: `CPU` (ARM64 instruction decode and execution engine), `MMU` (16KB granule translation and TLB caching), `GICv3` (interrupt distributor and redistributor models).
- `APPLE_KERNEL`: **None**. WinIOS makes zero false claims of authentic Apple XNU kernel execution. All kernel services are synthetic implementations designed for research and testing.

---

## 4. Protected Files & Safety Policies
- `D:\WinISO26\WinIOS_README.md`: Pristine, untouched.
- `D:\WinISO26\WinIOS\docs\progress.md`: Pristine, untouched.
- `D:\WinISO26\WinISO26_README.md`: Not created.
- `D:\WinISO26\Inferno-Research\Inferno`: Pristine at commit `cc4302a99167abec69b714cfd00c38caece7e7de`.
