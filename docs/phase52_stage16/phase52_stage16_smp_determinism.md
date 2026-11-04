# WinIOS Phase 52, Stage 16: SMP Determinism and Memory Ordering Specification

**Document Identifier:** `WINIOS-SPEC-52-16-004`  
**Status:** Approved Technical Specification  
**Date:** 2026-11-04  
**Author:** WinIOS System Architecture Team  
**Classification:** Core System Architecture Specification  

---

## 1. The Multi-Core Determinism Challenge

In single-core systems, an instruction stream is fundamentally sequential: given an identical initial state and identical non-deterministic inputs (timers, interrupts), execution is 100% deterministic.

In Symmetric Multiprocessing (SMP) systems, multiple CPU cores execute concurrently and communicate through shared memory. Even if each individual core is 100% deterministic in isolation, the **relative interleaving** of concurrent memory accesses across cores is non-deterministic:
- A load on Core 1 might execute either before or after a store on Core 0.
- If Core 0 writes to address $A$ and Core 1 reads from $A$, observing the old vs new value can radically alter the control flow of Core 1.
- Recording every single inter-core memory read and write would destroy performance and reintroduce the dense bandwidth bottlenecks that SNDL explicitly avoids.

This document establishes the formal SMP determinism model for WinIOS, analyzes atomic operations and memory barriers, evaluates deterministic multi-threading architectures, and defines the recommended scope progression for replay implementation.

---

## 2. WinIOS Current SMP Execution Architecture

An inspection of `src/cpu/smp_cluster.h` and `src/cpu/smp_cluster.cpp` reveals how WinIOS models multi-core execution:

1. **`SMPCluster` Structure:** Holds an array of `std::unique_ptr<CPU>` cores (e.g., 2, 4, or 8 cores), a shared `Memory` subsystem, a shared `GICv3` distributor, and a shared `PSCI` power controller.
2. **Execution Modes:**
   - **Mode A (Threaded):** Each simulated core runs in its own host thread (`std::jthread`). Synchronization occurs via C++ atomics, mutexes, and condition variables. While this delivers higher host throughput, memory access interleaving is governed by host OS thread scheduling and host CPU hardware memory model races.
   - **Mode B (Cooperative / Quantum Round-Robin):** A single master thread steps each active core sequentially by a fixed quantum $Q$ (e.g. $Q = 1000$ instructions or $Q = 1$ instruction):
     $$\text{Core 0 (steps 1..Q)} \longrightarrow \text{Core 1 (steps 1..Q)} \longrightarrow \dots \longrightarrow \text{Core N-1 (steps 1..Q)}$$
     Because the quantum and the core scheduling order are fixed, execution interleaving is completely deterministic.

---

## 3. Atomic Operations and Exclusive Monitors

AArch64 specifies two primary mechanisms for lockless synchronization and atomics: Load-Linked/Store-Conditional (LL/SC) and Large System Extensions (LSE).

### 3.1 Load-Exclusive / Store-Exclusive (`LDXR` / `STXR`)
The architectural behavior of exclusive accesses in AArch64 relies on local and global exclusive monitors:
1. `LDXR Xt, [Xn]`: Loads from memory and marks the physical address in the core's local exclusive monitor and the shared global exclusive monitor.
2. `STXR Ws, Xt, [Xn]`: Attempts to store to memory.
   - If the global monitor indicates that no other core has written to the exclusive reservation granule (typically 64 bytes) since the `LDXR`, the store succeeds: data is written, the monitor is cleared, and `Ws = 0`.
   - If another core modified the address granule, or if a context switch cleared the monitor (`CLREX`), the store fails: memory is **not** modified, and `Ws = 1`.

```
Core 0                                Core 1
  |                                     |
  +-- LDXR X0, [0x1000]                 |
  |   (Sets Monitor for Core 0)         |
  |                                     +-- STR X2, [0x1000]
  |                                     |   (Clears Core 0 Monitor!)
  |                                     |
  +-- STXR W1, X3, [0x1000]             |
  |   (Fails! W1 = 1, Memory Unchanged) |
```

#### Determinism Analysis
The success or failure of `STXR` is an explicit branch condition in guest software (e.g., a spinlock retry loop). In a non-deterministic multi-threaded host, recording the outcome of `STXR` (`Ws = 0` vs `Ws = 1`) via `SNDL_ATOMIC_OUTCOME` captures the exact race winner.

### 3.2 ARMv8.1-A Large System Extensions (LSE)
LSE provides single-instruction atomic read-modify-write operations:
- `SWP`, `SWPA`, `SWPL`, `SWPAL`: Atomic Swap.
- `LDADD`, `LDCLR`, `LDEOR`, `LDSET`: Atomic Fetch and Arithmetic/Bitwise op.
- `CAS`, `CASA`, `CASL`, `CASAL`: Compare and Swap.

In WinIOS, LSE instructions execute against host atomic primitives (`std::atomic_ref` in `src/memory/memory.cpp`). If executed in threaded SMP mode, the order in which two cores hit the atomic variable is non-deterministic. Recording the returned previous value ensures that both cores observe identical data during replay.

---

## 4. Memory Barriers and Ordering Semantics

AArch64 specifies three types of barriers:

1. **`DMB` (Data Memory Barrier):** Ensures that all explicit memory accesses occurring before the barrier are observed by other observers before any memory accesses occurring after the barrier.
2. **`DSB` (Data Synchronization Barrier):** Stricter than `DMB`. All memory accesses, cache maintenance operations, and TLB operations initiated before the barrier must be architecturally complete before any subsequent instruction can execute.
3. **`ISB` (Instruction Synchronization Barrier):** Flushes the processor pipeline, ensuring that subsequent instructions are fetched anew from cache/memory. Used after modifying system registers (`SCTLR_EL1`, `TTBR_EL1`) or self-modifying code.

### Determinism Impact
In a purely sequential simulator (or cooperative quantum scheduler), memory operations are naturally serialized in host memory. However, barriers define the points at which cores synchronize. 

---

## 5. Multi-Core Deterministic Replay Strategies

Three architectural strategies exist for handling SMP determinism in record-and-replay systems:

```
+---------------------------------------------------------------------------------------+
| SMP Replay Architecture Comparison                                                    |
|                                                                                       |
| Strategy 1: Shared Memory Conflict Logging (CREW / Lamport Clock)                     |
| - Run cores fully multithreaded.                                                      |
| - Intercept all conflicting shared memory reads/writes.                               |
| - High runtime recording overhead (50%-200%), massive log sizes.                      |
|                                                                                       |
| Strategy 2: Global Memory Lock (Lockstep Serialization)                               |
| - Cores execute concurrently for ALU, but acquire a global simulator mutex on memory.  |
| - Log the sequence of core IDs acquiring the memory lock.                            |
| - Moderate performance, moderate log size.                                            |
|                                                                                       |
| Strategy 3: Deterministic Multi-Threading (DMT) / Quantum Round-Robin Scheduling      |
| - Cores execute in fixed instruction quanta (e.g. Q = 1000 instructions).             |
| - Inter-core interleaving is an exact mathematical function of instruction count.     |
| - ZERO conflict logging required! Only timers and interrupts logged.                  |
| - 100% deterministic by construction.                                                 |
+---------------------------------------------------------------------------------------+
```

### 5.1 Analysis of Deterministic Multi-Threading (DMT)
Under DMT, the simulator schedules cores in fixed round-robin epochs:
1. Core 0 executes exactly $Q$ instructions.
2. Core 1 executes exactly $Q$ instructions.
3. Core $N-1$ executes exactly $Q$ instructions.
4. Advance virtual time and repeat.

**Why DMT is the Optimal Architectural Choice for WinIOS:**
- **Zero Log Overhead for Memory Races:** Because the relative schedule of all cores is fixed and repeatable, all race conditions and memory interleavings resolve in the exact same sequence on replay as they did during recording!
- **Simplicity and Reliability:** Eliminates the need to track dynamic memory conflict graphs or maintain fine-grained shadow cache coherence tags.
- **Dynamic Preemption on Synchronization:** If a core executes a blocking operation (`WFI`, `WFE`, or spinlock pause) or an inter-core PSCI event, the quantum terminates early and logs a `CORE_SYNC_STEP` record, recording the partial quantum.

---

## 6. Recommended Staged Replay Scope

To minimize architectural risk and deliver verified value incrementally, WinIOS defines a three-tier scope progression:

```
+------------------------------------------------------------------------------------+
| Tier 1: Single-Core Full Deterministic Replay                                      |
| - Scope: 1 CPU Core, Full Memory, GICv3, PMU, VirtualTime, SysRegs.               |
| - Inputs: Log CNTPCT/CNTVCT, MMIO reads, GIC interrupts, Keyframes.                |
| - Coverage: 95% of kernel boot, driver initialization, and single-thread execution. |
| - Complexity: Low / Highly Robust. Target for Stage 17–19.                         |
+------------------------------------------------------------------------------------+
                                          |
                                          v
+------------------------------------------------------------------------------------+
| Tier 2: Multi-Core Replay via Deterministic Quantum Scheduling (DMT)               |
| - Scope: 2 to 8 CPU Cores, Shared Memory, GICv3 SGI/PPI, PSCI multi-core boot.     |
| - Inputs: Tier 1 inputs + PSCI calls + CORE_SYNC_STEP partial quantum records.     |
| - Interleaving: Fixed round-robin instruction quantum (e.g. Q = 1000).             |
| - Coverage: Multi-core SMP OS boot, spinlocks, parallel workloads.                 |
| - Complexity: Medium. Target for Stage 21.                                         |
+------------------------------------------------------------------------------------+
                                          |
                                          v
+------------------------------------------------------------------------------------+
| Tier 3: Asynchronous Threaded Multi-Core Replay with Atomic Conflict Logging       |
| - Scope: True host multithreaded simulation (`std::jthread` per core).             |
| - Inputs: Tier 2 inputs + fine-grained LDXR/STXR outcomes + LSE atomic results.    |
| - Coverage: High-throughput SMP server simulation where DMT quantum pacing is slow.|
| - Complexity: High / Research grade. Target for Stage 25+.                         |
+------------------------------------------------------------------------------------+
```

### 6.1 Formal Recommendation for Phase 52 Continuation
WinIOS shall implement **Tier 1 (Single-Core Full Replay)** as the immediate first milestone in Stages 17–20, followed directly by **Tier 2 (Multi-Core DMT)** in Stage 21. This ensures that the snapshot engine, log serialization, instruction-boundary alignment, and divergence verification are exhaustively validated in a clean, race-free single-core environment before introducing multi-core scheduling.
