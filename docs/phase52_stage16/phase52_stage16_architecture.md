# WinIOS Phase 52, Stage 16: Snapshot + Non-Deterministic Log (SNDL) Architecture Specification

**Document Identifier:** `WINIOS-ARCH-52-16-001`  
**Status:** Approved Architecture Design  
**Date:** 2026-11-04  
**Author:** WinIOS System Architecture Team  
**Classification:** Core System Architecture Specification  

---

## 1. Executive Summary and Architectural Context

### 1.1 Purpose
This document defines the architectural specification for the WinIOS **Snapshot + Non-Deterministic Log (SNDL)** subsystem. In Phase 52 Stages 1–15, WinIOS established an architectural trace logging pipeline (`.wtr` format) capable of serializing 64-byte `TraceEvent` records for retired instructions, branches, memory accesses, exceptions, interrupts, system registers, timers, debug events, and CPU lifecycle transitions. 

In Stage 15, an exhaustive deterministic replay feasibility study concluded that while `.wtr` provides an invaluable audit and diagnostic feed, using it as a deterministic replay mechanism presents critical scalability and determinism barriers:
1. **Bandwidth Explosion:** Recording every instruction retirement, branch, and memory access generates 60–100 MB of trace per second of simulated execution at typical interpreter speeds (~1.2–1.5 MIPS).
2. **Execution Overhead:** Synchronous ring-buffer push and disk flushing introduce a 15–35% execution penalty.
3. **Re-execution vs Trace Playback:** A true replay engine must re-execute guest instructions deterministically rather than merely reading old state records, enabling arbitrary inspection, conditional breakpoints, and time-travel debugging.

The SNDL subsystem resolves these challenges by decoupling **dense execution telemetry** from **deterministic record-and-replay**. Instead of recording deterministic instruction outcomes, SNDL records only:
1. An initial complete architectural state checkpoint (**Snapshot**, `.wsnap`).
2. An asynchronous sequence of non-deterministic inputs and synchronization boundaries (**Non-Deterministic Log**, `.sndl`).

During replay, the emulator initializes its exact architectural state from the `.wsnap` checkpoint and re-executes all instructions using its deterministic CPU core. Whenever execution reaches an instruction retirement boundary matching a logged non-deterministic event (such as a timer register read, asynchronous interrupt injection, or MMIO read), the Replay Engine supplies the recorded value rather than reading live host hardware.

```
+-----------------------------------------------------------------------------------------+
|                                    RECORDING PHASE                                      |
|                                                                                         |
|  [Initial System State] ---> SnapshotManager ---> Creates Baseline Checkpoint (.wsnap)  |
|                                                                                         |
|  [CPU Interpreter] ----> Deterministic ALU / Branch / SysReg Execution (NOT Logged)    |
|         |                                                                               |
|         +--------------> Non-Deterministic Input Hit? (Timer, MMIO, Interrupt)         |
|                                |                                                        |
|                                v                                                        |
|                   NondeterminismRecorder ===> Appends Event to Ring Buffer (.sndl)      |
+-----------------------------------------------------------------------------------------+
                                             |
                                             v
+-----------------------------------------------------------------------------------------+
|                                      REPLAY PHASE                                       |
|                                                                                         |
|  [Baseline Checkpoint (.wsnap)] ---> ReplayEngine Restores Initial State                |
|                                             |                                           |
|  [Deterministic CPU Execution] <------------+                                           |
|         |                                                                               |
|         +--------------> Reaches Instruction Retirement Boundary?                       |
|                                |                                                        |
|                                v                                                        |
|                      Inject Logged Non-Deterministic Value from (.sndl)                 |
|                                |                                                        |
|                                v                                                        |
|                   VerificationEngine Compares Periodic State Hashes (Divergence Check)  |
+-----------------------------------------------------------------------------------------+
```

---

## 2. Coexistence with Existing `.wtr` Tracing Pipeline

The WinIOS architecture enforces strict separation of concerns between diagnostic event tracing (`.wtr`) and deterministic record-and-replay (`.sndl` + `.wsnap`).

### 2.1 Comparative Analysis

| Feature / Dimension | Diagnostic Architectural Trace (`.wtr`) | Snapshot + Non-Deterministic Log (SNDL) |
|---|---|---|
| **Primary Objective** | Observability, post-mortem audit, security monitoring | Bit-accurate deterministic re-execution, time-travel debugging |
| **Logged Artifacts** | Every instruction retired, branch taken, memory R/W, sysreg | Initial checkpoint (`.wsnap`) + non-deterministic inputs only |
| **Event Size** | Fixed 64 bytes (`TraceEvent`) | Variable record format (16 to 48 bytes depending on event type) |
| **Storage Rate** | 60–100 MB/sec at 1.5 MIPS (dense) | 10 KB–350 KB/sec typical (sparse, 200×–1000× reduction) |
| **Execution Impact** | 15–35% execution penalty | 1–3% execution penalty |
| **Replay Role** | Verification oracle / divergence comparator | Input driver for execution replay |
| **State Completeness** | Selective (ring buffer may drop events on overflow) | Absolute / Lossless (dropping an event breaks replay) |

### 2.2 Independent Operation and Orthogonality
1. **Dual-Stack Configuration:** Both subsystems can operate simultaneously or independently. During development, SNDL can record an execution while `.wtr` logs high-level lifecycle events.
2. **Replay Validation with `.wtr`:** When verifying a replay run, the Replay Engine can optionally enable `.wtr` output. Comparing the original `.wtr` with the replayed `.wtr` guarantees bit-level execution equivalence.
3. **No Schema Collision:** The 64-byte `TraceEvent` schema and its binary serialization remain 100% untouched. SNDL introduces separate file extensions (`.wsnap`, `.sndl`) and independent file headers.

---

## 3. Subsystem Architecture and Component Responsibilities

The SNDL subsystem consists of four primary components:
1. `SnapshotManager`: Checkpoint capture and restoration.
2. `NondeterminismRecorder`: Non-deterministic event interception and serialization.
3. `ReplayEngine`: Execution driver, event matching, and value injection.
4. `VerificationEngine`: Periodic state hashing and divergence detection.

```
+------------------------------------------------------------------------------------+
|                                    WinIOS Core                                     |
|                                                                                    |
|  +----------------------+  +---------------------+  +---------------------------+  |
|  |     CPU (AArch64)    |  |     Memory (RAM)    |  |   Peripherals & SysRegs   |  |
|  | Registers, PSTATE,   |  | Guest Physical      |  | GICv3, PMU, VirtualTime,  |  |
|  | Retirement Counter   |  | Memory Allocation   |  | PSCI, Debug Subsystem     |  |
|  +----------------------+  +---------------------+  +---------------------------+  |
|             ^                         ^                          ^                 |
|             |                         |                          |                 |
|             v                         v                          v                 |
|  +------------------------------------------------------------------------------+  |
|  |                          SnapshotManager Interface                           |  |
|  |    CaptureSystemState()                  RestoreSystemState()                |  |
|  +------------------------------------------------------------------------------+  |
|             ^                                                    ^                 |
|             | (during Recording)                                 | (during Replay) |
|             v                                                    v                 |
|  +--------------------------------------+   +-----------------------------------+  |
|  |        NondeterminismRecorder        |   |           ReplayEngine            |  |
|  | - Intercepts CNTPCT / MMIO / Ints    |   | - Restores .wsnap                 |  |
|  | - Tags with Instruction Retirement   |   | - Matches Retirement Count        |  |
|  | - SPSC Buffer -> .sndl Output        |   | - Injects Inputs / Preemptions    |  |
|  +--------------------------------------+   +-----------------------------------+  |
|                                                              |                     |
|                                                              v                     |
|                                             +-----------------------------------+  |
|                                             |        VerificationEngine         |  |
|                                             | - Periodic xxHash64 State Check   |  |
|                                             | - Divergence Trap & Diagnostics   |  |
|                                             +-----------------------------------+  |
+------------------------------------------------------------------------------------+
```

### 3.1 `SnapshotManager`
- **Role:** Captures a monolithic, point-in-time image of all persistent hardware state across all simulated components.
- **Responsibilities:**
  - Enumerate and serialize all CPU core states (GPRs X0–X30, PC, SP_ELx, PSTATE, SIMD V0–V31, FPCR/FPSR, EL0–EL3 system registers).
  - Enumerate and serialize all peripheral states (GICv3 Distributor, Redistributors, CPU interfaces, PMU, VirtualTime timers, PSCI affinity).
  - Snapshot guest physical memory pages, supporting both raw block storage and zero-page/sparse compression.
  - Calculate Section and Archive CRC32C/SHA-256 integrity signatures.
  - Restore state cleanly into existing or newly instantiated `CPU`, `Memory`, and `GICv3` instances, resetting all internal caches, TLBs, and pipeline flags.

### 3.2 `NondeterminismRecorder`
- **Role:** Intercepts external, asynchronous, or hardware-dependent inputs during simulation and writes timestamped, instruction-indexed event records.
- **Responsibilities:**
  - Record the exact core ID and `instruction_retirement_count` at the moment of each non-deterministic access.
  - Intercept time-query instructions (`CNTPCT_EL0`, `CNTVCT_EL0`, `PMCCNTR_EL0`).
  - Intercept peripheral MMIO read responses.
  - Intercept interrupt delivery boundaries from the GICv3.
  - Intercept pseudo-random register reads (`RNDR`, `RNDRRS`).
  - Buffer records into a lock-free Single Producer Single Consumer (SPSC) ring buffer to minimize execution-thread latency.
  - Stream records into a binary `.sndl` file with stream-level integrity blocks.

### 3.3 `ReplayEngine`
- **Role:** Coordinates deterministic re-execution of a recorded session.
- **Responsibilities:**
  - Direct the `SnapshotManager` to initialize system state from `.wsnap`.
  - Open and index `.sndl` records, caching upcoming non-deterministic events into an lookahead priority queue.
  - Step CPU cores. Before each instruction, check if an asynchronous event (e.g. interrupt injection) is scheduled for the current `instruction_retirement_count`.
  - Override system register reads and MMIO reads with recorded values.
  - Enforce cross-core synchronization and deterministic thread scheduling for SMP replays.

### 3.4 `VerificationEngine`
- **Role:** Continuously validates that re-executed simulation state perfectly mirrors the recorded run.
- **Responsibilities:**
  - Compute compact 64-bit cryptographic state hashes (`xxHash64`) of architectural registers and memory pages at periodic keyframe intervals (e.g., every 100,000 instructions).
  - Compare runtime state hashes against keyframe hashes stored in the `.sndl` stream.
  - On discrepancy, immediately suspend execution, capture a divergence diagnostic bundle (register diffs, instruction history, memory diffs), and produce an actionable failure report.

---

## 4. Architectural Interfaces and Contracts

The following C++20 conceptual interfaces define the boundaries between WinIOS core simulation components and the SNDL subsystem.

### 4.1 Checkpoint Serialization Interface (`ISnapshotSerializable`)

Every component that holds simulation state must implement `ISnapshotSerializable`:

```cpp
namespace winios::sndl {

enum class SnapshotSectionType : uint32_t {
    Header         = 0x01,
    CpuArchitectural = 0x02,
    MemoryPages    = 0x03,
    Gicv3Device    = 0x04,
    VirtualTime    = 0x05,
    PmuDevice      = 0x06,
    PsciState      = 0x07,
    Trailer        = 0xFF
};

struct SnapshotWriter {
    virtual ~SnapshotWriter() = default;
    virtual void WriteBytes(const void* data, size_t size) = 0;
    virtual void WriteSectionHeader(SnapshotSectionType type, uint32_t version, uint64_t payload_size) = 0;
};

struct SnapshotReader {
    virtual ~SnapshotReader() = default;
    virtual void ReadBytes(void* dest, size_t size) = 0;
    virtual bool SeekToSection(SnapshotSectionType type) = 0;
};

class ISnapshotSerializable {
public:
    virtual ~ISnapshotSerializable() = default;
    virtual bool SaveSnapshot(SnapshotWriter& writer) const = 0;
    virtual bool RestoreSnapshot(SnapshotReader& reader) = 0;
};

} // namespace winios::sndl
```

### 4.2 Non-Deterministic Hook Interface (`INondeterminismSink`)

```cpp
namespace winios::sndl {

enum class NondetEventType : uint8_t {
    CounterRead       = 0x01,
    MmioRead          = 0x02,
    InterruptInject   = 0x03,
    RandomNumberRead  = 0x04,
    AtomicOutcome     = 0x05,
    PsciInvocation    = 0x06,
    KeyframeHash      = 0x07
};

class INondeterminismSink {
public:
    virtual ~INondeterminismSink() = default;
    virtual void RecordCounterRead(uint32_t core_id, uint64_t inst_count, uint32_t reg_id, uint64_t value) = 0;
    virtual void RecordMmioRead(uint32_t core_id, uint64_t inst_count, uint64_t paddr, uint8_t size, uint64_t value) = 0;
    virtual void RecordInterruptInject(uint32_t core_id, uint64_t inst_count, uint32_t intid, uint8_t priority) = 0;
    virtual void RecordAtomicOutcome(uint32_t core_id, uint64_t inst_count, uint64_t paddr, bool exclusive_success, uint64_t old_val) = 0;
    virtual void RecordKeyframe(uint32_t core_id, uint64_t inst_count, uint64_t state_hash) = 0;
};

} // namespace winios::sndl
```

### 4.3 Replay Injection Interface (`IReplaySource`)

```cpp
namespace winios::sndl {

class IReplaySource {
public:
    virtual ~IReplaySource() = default;
    
    // Inquires whether an asynchronous event is due at the current retirement count
    virtual bool HasPendingEvent(uint32_t core_id, uint64_t inst_count) const = 0;
    
    // Injects synchronous or asynchronous values
    virtual uint64_t GetCounterRead(uint32_t core_id, uint64_t inst_count, uint32_t reg_id) = 0;
    virtual uint64_t GetMmioRead(uint32_t core_id, uint64_t inst_count, uint64_t paddr, uint8_t size) = 0;
    virtual bool ConsumePendingInterrupt(uint32_t core_id, uint64_t inst_count, uint32_t& out_intid, uint8_t& out_prio) = 0;
    virtual bool GetAtomicOutcome(uint32_t core_id, uint64_t inst_count, uint64_t paddr, uint64_t& out_old_val) = 0;
};

} // namespace winios::sndl
```

---

## 5. End-to-End Data Flow

### 5.1 Recording Lifecycle

```
[Start Session]
       |
       v
1. Pause simulated CPU cores
2. SnapshotManager::CaptureSystemState() -> writes baseline "checkpoint_0000.wsnap"
3. Configure CPU and MMU to route non-deterministic reads to NondeterminismRecorder
4. Resume simulated CPU cores
       |
       +---> For each instruction step:
       |        a. Increment CPU instruction_retirement_count
       |        b. If instruction reads CNTPCT/CNTVCT/PMCCNTR:
       |             Read host value -> NondeterminismRecorder::RecordCounterRead()
       |        c. If instruction performs MMIO Read:
       |             Perform device read -> NondeterminismRecorder::RecordMmioRead()
       |        d. If GIC delivers interrupt:
       |             Inject interrupt -> NondeterminismRecorder::RecordInterruptInject()
       |        e. At keyframe interval (e.g. 100k instructions):
       |             Compute xxHash64 of state -> NondeterminismRecorder::RecordKeyframe()
       |
[Stop Session]
       |
       v
5. Flush NondeterminismRecorder SPSC buffer to "session.sndl"
6. Write SNDL stream trailer with final instruction count and checksum
```

### 5.2 Replay Lifecycle

```
[Start Replay Session]
       |
       v
1. Allocate clean WinIOS execution environment (CPU, Memory, Peripherals)
2. SnapshotManager::RestoreSystemState("checkpoint_0000.wsnap")
3. ReplayEngine::LoadLog("session.sndl") -> Preloads upcoming events into lookahead queues
4. Configure CPU to route non-deterministic reads to ReplayEngine
       |
       +---> For each instruction step:
       |        a. Check ReplayEngine::HasPendingEvent(core_id, instruction_retirement_count)
       |           If pending interrupt:
       |             ReplayEngine::ConsumePendingInterrupt() -> Force GIC/CPU interrupt entry
       |        b. Execute instruction deterministically:
       |           If instruction reads CNTPCT/CNTVCT/PMCCNTR:
       |             Return ReplayEngine::GetCounterRead() [Do NOT read host timer!]
       |           If instruction performs MMIO Read:
       |             Return ReplayEngine::GetMmioRead() [Do NOT query physical device!]
       |        c. Increment CPU instruction_retirement_count
       |        d. At keyframe instruction count:
       |             Compute live state xxHash64
       |             Compare against recorded KeyframeHash
       |             If mismatch -> Trigger Divergence Abort & dump diagnostic bundle
       |
[End of Replay]
       |
       v
5. Verify total instruction count matches log trailer
6. Report successful 100% deterministic replay
```

---

## 6. Architectural Guarantees and Constraints

1. **Deterministic Execution Foundation:** The AArch64 CPU interpreter instruction execution logic (ALU, bitwise operations, address generation, branch target calculation, vector ops) must be strictly pure: identical input state and memory contents must yield identical output state and register updates.
2. **Instruction Retirement Boundary Granularity:** Instruction boundaries are indexed strictly by `instruction_retirement_count`. All asynchronous events (interrupts, wakeups) are delivered exclusively *between* instruction retirements, never mid-instruction.
3. **Lossless Log Transport:** The `.sndl` pipeline utilizes a blocking or bounded-drop-free logging policy. If the ring buffer fills, the simulator thread throttles/blocks rather than dropping records, preserving replay integrity.
4. **Zero Impact on `.wtr` Schema:** The 64-byte `TraceEvent` schema, `TraceFileWriter`, `TraceFileReader`, and `TraceAnalyzer` are completely unchanged and remain operational for dense tracing.
5. **No Speculative Hardware State:** The snapshot format only captures architectural state defined by the simulated models (ARMv8-A architecture, GICv3, PMU, Timers). No undefined or unmodeled hardware quirks are recorded.
