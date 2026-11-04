# WinIOS Phase 52, Stage 16: Non-Deterministic Log (.sndl) Specification

**Document Identifier:** `WINIOS-SPEC-52-16-003`  
**Status:** Approved Technical Specification  
**Date:** 2026-11-04  
**Author:** WinIOS System Architecture Team  
**Classification:** Core System Architecture Specification  

---

## 1. Overview and Rationale

The **Non-Deterministic Log (`.sndl`)** records the stream of all external, asynchronous, or non-deterministic inputs encountered during the execution of a simulated WinIOS guest.

### 1.1 The Deterministic Re-execution Thesis
An AArch64 instruction interpreter executing on a host computer is fundamentally a deterministic state machine:
$$\text{State}_{N+1} = \mathcal{F}(\text{State}_N, \text{Instruction}_N)$$

Whenever $\mathcal{F}$ is purely deterministic (e.g., adding two registers, branching based on flags, storing a register to memory), the output state is mathematically identical on every run without logging anything. Non-determinism enters the simulation solely through three channels:
1. **Asynchronous Preemption:** External interrupts breaking the instruction stream at an arbitrary point in time.
2. **Environment Queries:** Reading values that depend on the physical passage of host time or external physical devices (e.g. `CNTPCT_EL0`, hardware random numbers, MMIO registers).
3. **Multi-Threaded Interleaving:** Multiple cores racing on shared memory locations without an explicit serial order.

By recording only these non-deterministic events—tagged by the core's exact `instruction_retirement_count`—the `.sndl` format reduces the log bandwidth by several orders of magnitude while guaranteeing 100% bit-accurate replay.

---

## 2. Non-Deterministic Input Inventory in WinIOS

An audit of the WinIOS CPU interpreter (`src/cpu/cpu.cpp`), memory system (`src/memory/memory.cpp`), GICv3 (`src/cpu/gicv3.cpp`), and timer infrastructure (`src/cpu/virtual_time.cpp`) identifies the following complete taxonomy of non-deterministic events:

| Event ID | Name | Source / Trigger | Non-Deterministic Data Captured |
|:---:|---|---|---|
| `0x01` | `COUNTER_READ` | Reading `CNTPCT_EL0`, `CNTVCT_EL0`, or `PMCCNTR_EL0` | Register ID, 64-bit counter value |
| `0x02` | `INTERRUPT_INJECT` | GICv3 delivering an interrupt (PPI, SPI, SGI) to CPU | INTID (0..1023), Priority, Security Group |
| `0x03` | `MMIO_READ` | Load instruction reading from an unbacked MMIO device address | Physical address, access size (1..8 bytes), returned data |
| `0x04` | `RANDOM_READ` | Reading entropy registers `RNDR` or `RNDRRS` | 64-bit random value, NZCV success flag |
| `0x05` | `ATOMIC_OUTCOME` | `STXR`/`STLXR` exclusive store or LSE atomic (`CAS`, `SWP`, `LDADD`) | Physical address, success/fail flag, previous memory value |
| `0x06` | `PSCI_INVOCATION` | Core calling SMC/HVC for power state change (`CPU_ON`, `CPU_OFF`) | Function ID, Target MPIDR, Entry address, Context ID |
| `0x07` | `KEYFRAME_HASH` | Periodic verification beacon emitted every $K$ instructions | 64-bit state hash (`xxHash64`) for divergence detection |
| `0x08` | `CORE_SYNC_STEP` | SMP barrier or inter-core scheduling epoch marker | Scheduled core ID, instruction quantum length |

---

## 3. Binary File Format Layout

A `.sndl` file is structured as a fixed 64-byte file header, followed by a continuous stream of fixed-size 32-byte event records, terminated by an integrity trailer.

### 3.1 Fixed File Header (64 bytes)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       Magic: 'W' 'S' 'N' 'D' 'L' '\0' '\1' '\0'               |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Version              |          Core Count           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Base Snapshot UUID                      |
|                            (uint64)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Creation Timestamp                      |
|                            (uint64)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Total Record Count (uint64)                 |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Total Instructions (uint64)                 |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Flags (uint32)            |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Header CRC32C (uint32)     |    Reserved   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

```cpp
struct alignas(8) WSndlHeader {
    uint8_t  magic[8];             // 'W','S','N','D','L','\0','\1','\0'
    uint16_t version;               // Log version (1)
    uint16_t core_count;            // CPU core count
    uint32_t flags;                 // Bit 0: Has Keyframes, Bit 1: SMP Interleaved
    uint64_t base_snapshot_hash;    // SHA-256 (low 64 bits) of associated .wsnap
    uint64_t creation_timestamp_ns; // Host timestamp
    uint64_t total_record_count;    // Total records logged
    uint64_t total_instructions;    // Total instructions executed
    uint32_t header_crc32c;         // Checksum over bytes [0..55]
    uint32_t reserved;              // Padding
};
static_assert(sizeof(WSndlHeader) == 64, "WSndlHeader must be 64 bytes");
```

---

## 4. Uniform 32-Byte Record Structure

To maintain high throughput, cache alignment, and simple ring-buffer indexing, all `.sndl` records share a uniform 32-byte footprint.

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type (u8)   |   Core (u8)   |          Flags (u16)          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Sequence Number (uint32)                  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+               Instruction Retirement Count (uint64)           +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                        Payload Field 1 (uint64)               +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
+                        Payload Field 2 (uint64)               +
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

```cpp
struct alignas(8) WSndlRecord {
    uint8_t  event_type;                 // NondetEventType enum (1..8)
    uint8_t  core_id;                    // Originating CPU core index (0..127)
    uint16_t flags;                      // Event-specific flags
    uint32_t sequence_num;               // Monotonic record sequence index
    uint64_t instruction_retirement_count; // Exact CPU retirement counter
    uint64_t payload1;                   // Type-specific field 1
    uint64_t payload2;                   // Type-specific field 2
};
static_assert(sizeof(WSndlRecord) == 32, "WSndlRecord must be exactly 32 bytes");
```

### 4.1 Payload Mapping by Event Type

| Event Type | `payload1` Interpretation | `payload2` Interpretation | `flags` Interpretation |
|---|---|---|---|
| `COUNTER_READ` (`0x01`) | Register ID (e.g. `0xDF02` for `CNTPCT_EL0`) | Returned 64-bit Counter Value | None (0) |
| `INTERRUPT_INJECT` (`0x02`) | INTID (bits 0..15), Priority (bits 16..23) | GIC Target / CPU Interface Flags | Bit 0: Group 1, Bit 1: Secure |
| `MMIO_READ` (`0x03`) | Guest Physical Address (`paddr`) | Returned Read Data (64-bit zero-extended) | Access Size in bytes (1, 2, 4, 8) |
| `RANDOM_READ` (`0x04`) | Returned Entropy Value | None (0) | Bit 0: Success (1) / Failure (0) |
| `ATOMIC_OUTCOME` (`0x05`) | Physical Address (`paddr`) | Previous Value in Memory (`old_val`) | Bit 0: Exclusive Store Succeeded |
| `PSCI_INVOCATION` (`0x06`) | PSCI Function ID (e.g. `CPU_ON`) | Target MPIDR / Parameter | Return Status / Context |
| `KEYFRAME_HASH` (`0x07`) | State Hash (`xxHash64`) | Memory Hash Digest (low 64 bits) | Checkpoint Sequence Number |
| `CORE_SYNC_STEP` (`0x08`) | Target Core ID | Quantum Step Count (Instructions) | Barrier Reason Code |

---

## 5. Ordering and Boundary Semantics

### 5.1 Exact Instruction-Retirement Indexing
In WinIOS, every CPU core maintains an internal monotonically increasing counter:
```cpp
uint64_t instruction_retirement_count_{0};
```
This counter is incremented **if and only if** an architectural instruction reaches completion (i.e., all register and memory writes have committed). 

### 5.2 Event Ordering Rules
1. **Synchronous Events (e.g. `COUNTER_READ`, `MMIO_READ`, `RANDOM_READ`):**
   - Occur *during* the execution of a load/system-register instruction.
   - Tagged with the `instruction_retirement_count` that the instruction will have *upon retirement*.
   - During replay, when an instruction at count $N$ requests counter data, the Replay Engine matches the event record with count $N$ and supplies `payload2`.
2. **Asynchronous Events (e.g. `INTERRUPT_INJECT`):**
   - Occur *between* instruction retirements.
   - Tagged with the exact count $N$ of instructions retired *immediately prior* to interrupt entry.
   - During replay, after instruction $N$ retires, the Replay Engine checks for pending interrupts tagged at count $N$. If matched, it triggers exception vector entry before instruction $N+1$ begins.
3. **Keyframe Hashes (`KEYFRAME_HASH`):**
   - Emitted immediately following instruction $N$.
   - Includes state of all registers after instruction $N$ has retired.

```
       Instruction N-1 Retires (Count = N-1)
                     |
                     v
   [Check: Pending Interrupt for Count N-1?]  <--- Asynchronous Injection Boundary
                     |
                     v
       Instruction N Begins Execution
         |
         +--> Reads CNTPCT_EL0 -> Returns Logged Value (Count = N)
         |
       Instruction N Retires (Count = N)
                     |
                     v
   [Check: Pending Interrupt for Count N?]    <--- Next Boundary
```

---

## 6. Simulation Injection Hooks in WinIOS

To implement SNDL recording and replay, precise hooks must be placed at the core architectural interfaces in WinIOS:

### 6.1 System Register Read Hook (`src/cpu/cpu.cpp`)
```cpp
uint64_t CPU::read_system_register(SystemRegister reg) {
    if (reg == SystemRegister::CNTPCT_EL0 || reg == SystemRegister::CNTVCT_EL0) {
        if (replay_mode_active_) {
            return replay_source_->GetCounterRead(core_id_, instruction_retirement_count_ + 1, static_cast<uint32_t>(reg));
        }
        uint64_t val = virtual_time_->read_counter(reg);
        if (record_mode_active_) {
            nondet_sink_->RecordCounterRead(core_id_, instruction_retirement_count_ + 1, static_cast<uint32_t>(reg), val);
        }
        return val;
    }
    // Normal register read...
}
```

### 6.2 MMIO Memory Read Hook (`src/memory/memory.cpp`)
```cpp
uint64_t Memory::read_mmio(uint64_t paddr, uint8_t size) {
    if (replay_mode_active_) {
        return replay_source_->GetMmioRead(active_core_id_, active_cpu_->get_retirement_count() + 1, paddr, size);
    }
    uint64_t val = mmio_router_->dispatch_read(paddr, size);
    if (record_mode_active_) {
        nondet_sink_->RecordMmioRead(active_core_id_, active_cpu_->get_retirement_count() + 1, paddr, size, val);
    }
    return val;
}
```

### 6.3 Asynchronous Interrupt Hook (`src/cpu/cpu.cpp`)
```cpp
void CPU::step() {
    // 1. Asynchronous Event Injection Check
    if (replay_mode_active_) {
        uint32_t intid;
        uint8_t prio;
        if (replay_source_->ConsumePendingInterrupt(core_id_, instruction_retirement_count_, intid, prio)) {
            deliver_interrupt_architectural(intid, prio);
            return;
        }
    } else {
        // Live GIC check
        if (gic_->has_pending_interrupt(core_id_)) {
            auto [intid, prio] = gic_->acknowledge_interrupt(core_id_);
            if (record_mode_active_) {
                nondet_sink_->RecordInterruptInject(core_id_, instruction_retirement_count_, intid, prio);
            }
            deliver_interrupt_architectural(intid, prio);
            return;
        }
    }

    // 2. Deterministic Instruction Execution
    execute_instruction(fetch_instruction());
    instruction_retirement_count_++;

    // 3. Periodic Keyframe Hash Check
    if ((instruction_retirement_count_ % KEYFRAME_INTERVAL) == 0) {
        uint64_t h = compute_architectural_hash();
        if (record_mode_active_) {
            nondet_sink_->RecordKeyframe(core_id_, instruction_retirement_count_, h);
        } else if (replay_mode_active_) {
            verification_engine_->VerifyKeyframe(core_id_, instruction_retirement_count_, h);
        }
    }
}
```
