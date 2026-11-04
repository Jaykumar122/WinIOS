# WinIOS: AArch64 C++20 Execution Environment & Deterministic SMP Replay Architecture

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%20x64-0078D6.svg)](https://www.microsoft.com/windows)
[![Tests](https://img.shields.io/badge/CTest-63%2F63%20Passing%20(100%25)-brightgreen.svg)]()
[![Deterministic SMP Replay](https://img.shields.io/badge/SMP%20Replay-4--Core%20Verified%20(Bit--Identical)-success.svg)]()

WinIOS is an experimental, high-assurance AArch64 (ARM64) interpreter, system emulation environment, and deterministic symmetric multiprocessing (SMP) execution and record/replay platform engineered in modern C++20 for Windows.

---

## About This Project

WinIOS provides a mathematically reproducible, cycle-disciplined foundation for AArch64 research on Windows. Designed from first principles with an emphasis on strict determinism, formal state verification, and total session isolation, WinIOS eliminates undefined host dependencies by routing all architectural time, memory ordering, and asynchronous external events through an authoritative single-host-thread scheduler and recording pipeline.

### Core Architecture Highlights

- **AArch64 Core Engine:** Full 64-bit architectural state ($X0..X30$, $SP$, $PC$, $NZCV$, $DAIF$, $PSTATE$) supporting user, kernel, hypervisor, and secure monitor privilege modes (EL0, EL1, EL2, EL3).
- **Virtual Memory & MMU:** Multi-level translation table walks (4KB granule), Stage-1 and Stage-2 translation, translation caching, access permissions, memory attributes, and fault reporting.
- **System IP & Peripherals:** ARM Generic Interrupt Controller v3 (GICv3 SPI/PPI/SGI), ARM Generic Timers (`CNTPCT_EL0`, `CNTVCT_EL0`, `CNTFRQ_EL0`), Performance Monitors Unit (PMU), and PSCI power state coordination.
- **Deterministic SMP Replay Engine:** Genuine 1-core, 2-core, and 4-core multi-core deterministic replay driven by `.sndl v2` event capture and `.wsnap v2` architectural snapshots. Guaranteed 100% bit-identical canonical cluster state hashes across repeated executions ($H_{\text{rep}}^{(1..10)} = \text{0xb8e9040fc54a7535}$).
- **Hardware-Assisted Debug Architecture:** Hardware breakpoints, data watchpoints, software stepping, and linked context-aware debug events.
- **Diagnostic Tracing Subsystem (`.wtr v1`):** Lock-free, zero-allocation circular ring buffers serializing instruction traces, memory accesses, register mutations, sysreg accesses, and interrupt lifecycles.
- **Architectural Snapshot Engine (`.wsnap v1 & v2`):** Point-in-time state serialization covering complete cluster state, register contexts, and zstd-compressed physical RAM with CRC32C and Blake3 canonical state hashing.
- **Nondeterministic Event Logging (`.sndl v1 & v2`):** Bounded streaming binary event ledger capturing MMIO reads, entropy/RNG queries, external interrupts, virtual timers, and load/store exclusive monitor outcomes.

---

## Architectural Topology

```
+---------------------------------------------------------------------------------------+
|                                 SmpReplayCoordinator                                  |
|   - Replay State Machine (Uninitialized -> Prepared -> Replaying -> Completed)        |
|   - Multi-Core Capability Gate (1-Core, 2-Core, 4-Core Opt-In; 8-Core Strict Reject)  |
|   - Clock Source Virtualization: DeterministicVirtualClock (BLK-SMP-04 Resolved)      |
|   - DMA Capability Protection (BLK-SMP-03 Active Blocker Mitigation)                  |
+-------------------------------------------+-------------------------------------------+
                                            |
                +---------------------------+---------------------------+
                |                                                       |
                v                                                       v
     +--------------------+                                  +--------------------+
     |   .wsnap v2 File   |                                  |   .sndl v2 File    |
     | (Cluster RAM & Reg)|                                  | (Recorded Events)  |
     +----------+---------+                                  +----------+---------+
                |                                                       |
                v                                                       v
     +----------------------+                                +----------------------+
     |  SmpSnapshotRestorer |                                |  SndlV2ReplayReader  |
     |  - Cluster Memory    |                                |  - Bounded Streaming |
     |  - Per-Core State    |                                |  - CRC32C Validation |
     |  - Initial Hash (H0) |                                +----------+-----------+
     +----------------------+                                           |
                                                                        v
                                                             +----------------------+
                                                             | SmpReplayEventCursor |
                                                             | - Monotonic Ordering |
                                                             | - Lookahead Peek     |
                                                             +----------+-----------+
                                                                        |
     +------------------------------------------------------------------+
     |
     v
+---------------------------------------------------------------------------------------+
| Four-Core Deterministic Scheduler & Replay Execution Loop (Single Host Thread)        |
|                                                                                       |
| 1. Authoritative SmpScheduler selects active core in [0..3] via Round-Robin.          |
| 2. Event cursor validates CoreSelected and QuantumStart matches.                      |
| 3. Step active core instructions, advancing R_core[i] and global R_global.            |
| 4. Deterministic event injection (MMIO, Entropy, IRQ, Timers, Store-Exclusive).       |
| 5. Validate QuantumEnd and EarlyTruncation bounds.                                    |
| 6. Compute final canonical multicore hash at completion: H_rep == H_expected.         |
+---------------------------------------------------------------------------------------+
```

---

## Subsystem Architecture & Features

### 1. AArch64 Processor & Memory
- **Registers:** 31 64-bit general purpose registers (`X0`..`X30`), dedicated stack pointer (`SP`), program counter (`PC`), and processor state flags (`PSTATE`/`NZCV`).
- **Memory Operations:** Endian-safe byte, halfword, word, and doubleword accesses with bounds enforcement.
- **MMU Subsystem:** 4-level paging (L0..L3), page table walker, Stage 1 / Stage 2 translation, translation lookaside buffers (TLB), memory protection, and Data/Instruction Abort routing.
- **Exclusive Monitor (`SmpExclusiveMonitorManager`):** 64-byte granule tracking for `LDXR`/`STXR` synchronization across cores. Cross-core stores invalidate active reservations deterministically.

### 2. Privilege Levels & System Control
- **Exception Levels:** Discrete modeling of EL0 (Application), EL1 (OS Kernel), EL2 (Hypervisor), and EL3 (Secure Monitor).
- **System Registers:** Architectural access checks for `SCTLR_EL1`, `TTBR0/1_EL1`, `TCR_EL1`, `MAIR_EL1`, `VBAR_EL1`, `ESR_EL1`, `FAR_EL1`, `CPACR_EL1`, `CNTV_CTL_EL0`, and generic timer registers.
- **GICv3 Controller:** Distributed interrupt controller handling Software Generated Interrupts (SGI 0..15), Private Peripheral Interrupts (PPI 16..31), and Shared Peripheral Interrupts (SPI 32..1019).
- **Virtual Time Engine:** All architectural time sources are driven strictly by retired instruction counts ($\text{VirtualTicks} = \sum R_{\text{core}}[i]$), ensuring absolute host-independence.

### 3. Record & Replay Pipeline
- **SNDL v2 Wire Format:** 80-byte aligned binary event record format encoding sequence numbers, event types, core IDs, local retirement timestamps ($R_{\text{core}}$), global retirement timestamps ($R_{\text{global}}$), payloads, and CRC32C integrity hashes.
- **Streaming Replay Cursor:** Zero-copy lookahead cursor buffering event streams and verifying monotonic progression.
- **Capability Gating:** Replay core topologies are strictly checked. Single-core, two-core, and four-core topologies are fully validated; unsupported eight-core configurations are rejected with `ReplayErrorCode::UnsupportedConfiguration`.
- **Divergence Detection:** Immediate halt upon first divergence, recording full diagnostic telemetry: core ID, program counter, instruction word, retirement count, expected hash vs actual hash.

---

## Repository Layout

```text
WinIOS/
├── CMakeLists.txt              # Top-level build configuration (MSVC / C++20)
├── README.md                   # Project overview, architecture, and guides
├── PHASE52_STAGE27_REPORT.md   # Stage 27 verification and conformance report
├── PHASE52_STAGE27_ARCHITECTURE.md # Detailed 4-core replay architecture specification
├── src/
│   ├── cpu/                    # AArch64 CPU, MMU, GICv3, PMU, PSCI, SMP Cluster
│   ├── memory/                 # Physical memory model, bounds checking, paging
│   ├── trace/                  # .wtr diagnostic tracing pipeline & ring buffer
│   ├── snapshot/               # .wsnap v1 & v2 cluster snapshot engine
│   ├── sndl/                   # .sndl v1 & v2 nondeterministic event recording
│   ├── replay/                 # SmpReplayCoordinator, cursor, and replay sessions
│   └── main.cpp                # System demonstration entry point
├── tests/
│   ├── unit/                   # Unit test suites (CPU, MMU, SMP, Replay, etc.)
│   └── integration/            # Multi-component integration test pipelines
├── tools/                      # Offline analysis utilities & trace inspectors
└── docs/                       # Phased architecture and audit deliverables
```

---

## Building and Running

### Prerequisites
- **Operating System:** Windows 10/11 x86_64
- **Compiler:** Visual Studio 2022/2026 (MSVC with full C++20 standard conformance)
- **Build System:** CMake 3.25 or higher

### Build Instructions (PowerShell)

```powershell
# 1. Configure the build directory
cmake -S . -B build -G "Visual Studio 18 2026" -A x64

# 2. Compile the project (Release or Debug)
cmake --build build --config Release

# 3. Run the demonstration executable
.\build\Release\win_ios.exe
```

---

## Running Test Suites

WinIOS maintains 63 CTest targets with a **100% pass rate** in both Release and Debug configurations:

```powershell
# Run the complete test suite
ctest --test-dir build -C Release --output-on-failure

# Execute the 4-core deterministic replay test suite (18 test groups)
.\build\Release\smp_four_core_replay_tests.exe

# Execute the 4-core deterministic replay benchmark
.\build\Release\smp_four_core_replay_benchmark.exe
```

### Four-Core Replay Test Matrix (18 Groups)

| Group | Focus Area | Verification Result |
|:---:|---|:---:|
| **1** | Four-Core Capability Gate (`allow_four_core_replay`) | **PASS** |
| **2** | Snapshot Restoration (`.wsnap v2`) & Canonical Hash $H_0$ | **PASS** |
| **3** | Four-Core Event Cursor Monotonicity & Lookahead | **PASS** |
| **4** | Four-Core Scheduler Replay Coordination & Slices | **PASS** |
| **5** | Global Retirement Synchronization ($R_{\text{global}}$) | **PASS** |
| **6** | Per-Core Retirement Synchronization ($R_{\text{core}}[0..3]$) | **PASS** |
| **7** | Shared Memory Interleaving & Determinism | **PASS** |
| **8** | Exclusive Monitor Contention (64-byte granule, `LDXR`/`STXR`) | **PASS** |
| **9** | Multi-Core External Interrupt Replay (GICv3 SPI/PPI) | **PASS** |
| **10** | Virtual Generic Timer Replay (`CNTPCT`/`CNTVCT`) | **PASS** |
| **11** | MMIO and Entropy Input Replay | **PASS** |
| **12** | DMA Capability Protection & Rejection (BLK-SMP-03) | **PASS** |
| **13** | Host Clock Virtualization & Rejection (BLK-SMP-04) | **PASS** |
| **14** | Repeated Replay (10 Runs Bit-Identical Hash Match) | **PASS** |
| **15** | Eight-Core Replay Rejection (`UnsupportedConfiguration`) | **PASS** |
| **16** | Divergence Detection & Failure Injection | **PASS** |
| **17** | Two-Core Replay Regression Conformance | **PASS** |
| **18** | Single-Core Replay Session Regression Conformance | **PASS** |

---

## Performance Benchmarks

Measured using `smp_four_core_replay_benchmark.exe` (Release build, MSVC /O2, x86_64):

| Metric | Target / Budget | Measured Result | Performance Margin |
|---|---|---|---|
| **Event Replay Throughput** | $> 100,000$ events/s | **161,660 events/s** | **+61.66% above budget** |
| **Replay Execution Speed** | $> 4.0$ MIPS | **5.39 MIPS** | **+34.75% above budget** |
| **Scheduler Coordination Overhead** | $< 100\%$ | **84.93 %** | **Well within 100% budget** |
| **Snapshot Restore (4 Cores + 4MB RAM)** | $< 15.0$ ms | **10.80 ms** | **28.0% faster than budget** |
| **Final Canonical Hash Computation** | $< 5.0$ ms | **1.88 ms** | **62.4% faster than budget** |
| **Replay Memory Delta (Heap RSS)** | $< 16.0$ MB | **4.10 MB** | **74.37% below budget** |
| **Final Canonical Hash Match** | Exact Match | **0xd6266bced108ef0b** | **100% BIT-IDENTICAL** |

---

## License & Attribution

This project is an independent research endeavor focused on processor architecture, deterministic execution, and emulation engineering. ARM and AArch64 are registered trademarks of Arm Limited.
