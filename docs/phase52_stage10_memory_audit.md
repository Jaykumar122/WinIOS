# WinIOS Phase 52 — Stage 10: Memory Access Architecture Audit

**Document Version**: 1.0.0  
**Phase**: 52 (Execution & Diagnostic Tracing Pipeline)  
**Stage**: 10 (Memory Read/Write Trace Integration)  
**Date**: November 2026  
**Status**: APPROVED / ARCHITECTURAL AUDIT COMPLETE  

---

## 1. Executive Summary

This architecture audit comprehensively inspects the entire guest memory access pipeline of the WinIOS ARM64 emulator in preparation for Stage 10 memory tracing integration (`TraceEventType::MemoryRead` [7] and `TraceEventType::MemoryWrite` [8]).

The audit investigates:
1. The exact instruction decoding and execution paths for all guest loads, stores, pair operations, exclusive monitors, atomics, and SIMD vector transfers.
2. The effective address computation, memory-address wrapper layer (`CheckedGuestMemory`), translation pipeline (`mmu::MMU`), and backing physical memory (`Memory`).
3. Alignment validation, permission verification, synchronous exception generation (Data Aborts), and architectural fault unwinding.
4. Memory-mapped I/O (MMIO) dispatch for GICv3 distributor and redistributor regions.
5. Trace event generation semantics, event field mapping under the fixed 64-byte `TraceEvent` schema, address representation (VA vs. PA), and ordering relative to instruction retirement.

---

## 2. Architectural Pipeline & Call Graph

The guest memory pipeline operates in clearly stratified layers:

```
[ AArch64 Guest Instruction Fetch ] (guest_memory.read_u32)
                   │
                   ▼
[ Instruction Decode & Operand Extraction ]
  - Base register (Rn / SP)
  - Offset (imm9, imm12, simm7, Rm extended)
  - Target register(s) (Rt, Rt2, status)
                   │
                   ▼
[ Effective Address Calculation ]
  - ea = base + offset (modulo 2^64)
                   │
                   ▼
[ CheckedGuestMemory Bridge ]
  - Natural alignment check (Device memory / SCTLR_EL1.A)
  - Watchpoint detection (winios::DebugArchitecture)
  - PMU event recording (ARMV8_EVENT_MEM_ACCESS, L1D_CACHE_*)
                   │
                   ├──> [ Translation Fault / Permission Fault / Watchpoint ]
                   │         │
                   │         ▼
                   │    throw SynchronousExceptionSignal (Unwind CPU step)
                   │    - 0 MemoryRead / MemoryWrite events emitted
                   │    - 0 InstructionRetired events emitted
                   │    - 1 ExceptionEntered event emitted at vector entry
                   │
                   ▼
[ Address Translation (mmu::MMU) ]
  - If MMU off: PA = EA
  - If MMU on: Stage 1 / Stage 2 Walk (TLB lookup -> Page Table walk -> PA)
                   │
                   ▼
[ Physical Memory & MMIO Dispatch ]
  ├── GIC MMIO? ────> GICv3::read_mmio / GICv3::write_mmio
  └── Guest RAM? ───> Memory::read_u* / Memory::write_u* (with striped locks)
                   │
                   ▼
[ Stage 10 Memory Trace Emission ] (ONLY upon successful completion)
  - TraceEventType::MemoryRead (7) or MemoryWrite (8)
  - Monotonic sequence number N
  - Original instruction PC, VA / PA, access width, transferred data value
                   │
                   ▼
[ Architectural Register Commit & Normal Store Tracking ]
  - Destination register(s) updated
  - note_normal_store() for exclusive monitor invalidation
                   │
                   ▼
[ Instruction Retirement Trace Emission ] (emit_trace())
  - TraceEventType::InstructionRetired (1)
  - Monotonic sequence number N + 1
```

---

## 3. Detailed Audit of Memory Pipeline Subsystems

### 3.1 CPU Load & Store Instruction Handlers
Guest memory operations are categorized into distinct instruction groups:
1. **Unsigned Immediate Loads & Stores (`kLoadStoreUnsignedMask = 0xFFC00000u`)**:
   - `LDR` / `STR` (32-bit & 64-bit general purpose).
   - `LDRB` / `STRB`, `LDRH` / `STRH`.
   - `LDRSB` (32/64-bit sign-extended), `LDRSH` (32/64-bit sign-extended), `LDRSW` (64-bit sign-extended).
2. **Indexed Loads & Stores (`kLoadStoreIndexedMask = 0xFFE00C00u`)**:
   - Pre-indexed: `base = base + simm9; ea = base; writeback to base`.
   - Post-indexed: `ea = base; writeback = base + simm9; writeback to base`.
   - Covers 32-bit and 64-bit `LDR` / `STR`.
3. **Unscaled Immediate Loads & Stores (`kLoadStoreUnscaledMask = 0xFFE00C00u`)**:
   - `LDUR` / `STUR` with unscaled 9-bit signed immediate (`simm9`).
   - Supports 8-bit, 16-bit, 32-bit, and 64-bit widths.
4. **Register Offset Loads & Stores (`kLoadStoreRegOffMask = 0xFFE00C00u`)**:
   - Offset supplied via general-purpose register `Rm` with extension (`UXTW`, `UXTX`, `SXTW`, `SXTX`) and optional scaling shift.
5. **Load / Store Pair (`kLoadStorePairMask = 0xFFC00000u`)**:
   - `LDP` / `STP` (32-bit and 64-bit), `LDPSW`.
   - Transfers two adjacent elements (`ea1 = ea`, `ea2 = ea + scale`).
   - Atomicity & fault pre-checking: `memory.check_access()` validates both elements before any memory or register modification.
6. **Literal PC-Relative Loads (`kLoadLiteralFamilyMask = 0x3F000000u`)**:
   - `LDR W`, `LDR X`, `LDRSW` literal: offset from current instruction PC via signed 19-bit immediate scaled by 4.
7. **SIMD & Floating-Point Loads & Stores (`0x3C000000u`)**:
   - Scalar FP: `B` (1B), `H` (2B), `S` (4B), `D` (8B).
   - Vector: `Q` (16B / 128-bit) executed as two atomic 64-bit memory accesses (`low` at `ea`, `high` at `ea + 8`).
8. **Exclusive Operations (`kExclusiveGroupMask = 0x3F000000u`)**:
   - `LDXR` / `LDAXR` (Load Exclusive, 8/16/32/64-bit).
   - `STXR` / `STLXR` (Store Exclusive, 8/16/32/64-bit).
   - `LDXP` / `LDAXP` (Pair Load Exclusive, 2x32-bit or 2x64-bit).
   - `STXP` / `STLXP` (Pair Store Exclusive, 2x32-bit or 2x64-bit).
   - `LDAR` / `STLR` (Load-Acquire / Store-Release).
9. **LSE Atomic Memory Operations (`kLseAtomicMask = 0x3B200C00u`)**:
   - Atomic Read-Modify-Write: `LDADD`, `LDCLR`, `LDEOR`, `LDSET`, `LDSMAX`, `LDSMIN`, `LDUMAX`, `LDUMIN`, `SWP`.
   - Widths: 8-bit, 16-bit, 32-bit, 64-bit.
10. **Block Cache Operations**:
    - `DC ZVA`: Zero 64-byte cache block via eight consecutive 64-bit zero stores.

---

### 3.2 Addressing Modes & Effective Address Calculation
Effective addresses are computed using 64-bit modulo arithmetic (`checked_add_address`, `add_signed_address`, `add_extended_offset_address`). In architectural exception mode, address calculations follow AArch64 wrapping rules ($2^{64}$).

### 3.3 Virtual-to-Physical Translation & TLB Subsystem
The MMU (`mmu::MMU`) implements:
- **Regimes**: EL1&0 Stage 1 translation; EL2 Stage 2 translation.
- **Granule**: 4 KB granule with 48-bit address space.
- **Descriptors**: Invalid (0b00), Block (0b01 at L1/L2), Table (0b11 at L0-L2), Page (0b11 at L3).
- **Software TLB**: 64-entry fully associative TLB with ASID and VMID validation.
- **Zero Extra Translation Rule**: Tracing hooks must NEVER call `mmu.translate()` independently, as doing so would contaminate TLB state or cause spurious page-walk side effects. Instead, tracing captures the translation result obtained during the architectural memory access.

### 3.4 Permission Checks & Data Aborts
- **Access Permissions**: Evaluated against AP[2:1], Privileged/Unprivileged mode (`current_el_ > 0`), and Read/Write/Execute kind.
- **Fault Generation**: Translation, Address-Size, Access-Flag, and Permission faults construct a `SynchronousException` containing:
  - ESR_EL1 with EC = 0x24 / 0x25 (Data Abort from Lower / Same EL) and 6-bit FSC.
  - FAR_EL1 set to the faulting guest virtual address.
  - WnR bit indicating whether the aborting access was a write.
- **Unwinding**: The memory access throws `SynchronousExceptionSignal`, completely unwinding `CPU::step()` before any register writeback or trace submission occurs.

### 3.5 Physical Memory Dispatch (`Memory`)
- Backing store: `std::vector<std::uint8_t> bytes_` allocated at guest startup.
- Concurrency: Striped `shared_mutex` array (4 KB per stripe). In single-threaded execution, unlocked access is preserved.
- Store Observer & Store Serialization: Global exclusive-monitor reservations are invalidated on stores via `note_normal_store()`.

### 3.6 Memory-Mapped I/O (MMIO)
- The emulator routes physical addresses within the GICv3 distributor (`GICD`: `0x08000000..0x0800FFFF`) and redistributor (`GICR`: `0x080A0000..0x0817FFFF`) directly to `winios::GICv3::read_mmio()` and `write_mmio()`.
- **Side-effect preservation**: MMIO reads can be read-to-clear; MMIO writes can be write-one-to-clear. Tracing hooks MUST NOT re-read MMIO registers. Tracing captures the exact value returned by `read_mmio()` or passed to `write_mmio()`.

### 3.7 Atomic & Exclusive Operation Semantics
- **Exclusive Monitor**: Per-CPU `ExclusiveMonitor` tracks reservation address, size, and loaded value.
- **STXR Behavior**: If reservation is lost or invalidated, `STXR` writes nothing to memory and sets status register to 1. **Under no circumstances does a failed exclusive store emit a `MemoryWrite` or `MemoryRead` event.**
- **LSE RMW Atomics**: Atomically load `old_value` and store `new_value`. In Stage 10, both the read aspect and the write aspect are traced with `TraceEventFlags::IsAtomic` set.

---

## 4. Stage 10 Trace Event Mapping & Schema Alignment

### 4.1 Schema Layout (Fixed 64-Byte POD)

| Field | Size | MemoryRead (7) Semantics | MemoryWrite (8) Semantics |
|---|---|---|---|
| `sequence_number` | 8 B | Monotonic sequence $N$ | Monotonic sequence $N$ |
| `virtual_time` | 8 B | Authoritative virtual counter ticks | Authoritative virtual counter ticks |
| `pc` | 8 B | Instruction PC initiating the load | Instruction PC initiating the store |
| `address` | 8 B | Primary address (VA if MMU on, PA if MMU off) | Primary address (VA if MMU on, PA if MMU off) |
| `data` | 8 B | Transferred value read from memory | Transferred value written to memory |
| `opcode_or_syndrome` | 4 B | 32-bit AArch64 instruction opcode | 32-bit AArch64 instruction opcode |
| `flags` | 4 B | `HasData \| [HasVirtualAddress \| HasPhysicalAddress] \| [IsPrivileged] \| [IsAtomic]` | `HasData \| IsWrite \| [HasVirtualAddress \| HasPhysicalAddress] \| [IsPrivileged] \| [IsAtomic]` |
| `event_type` | 2 B | `TraceEventType::MemoryRead` (7) | `TraceEventType::MemoryWrite` (8) |
| `core_id` | 2 B | Emulating CPU core ID | Emulating CPU core ID |
| `exception_level` | 1 B | Current EL (0, 1, 2, 3) | Current EL (0, 1, 2, 3) |
| `address_space` | 1 B | `Virtual` (1) if MMU on, `Physical` (2) if off | `Virtual` (1) if MMU on, `Physical` (2) if off |
| `access_size` | 2 B | Width in bytes (1, 2, 4, 8) | Width in bytes (1, 2, 4, 8) |
| `reserved` | 8 B | Strictly `0x0000000000000000` | Strictly `0x0000000000000000` |

### 4.2 Schema Address Field Analysis & Documented Limitation
The fixed 64-byte `TraceEvent` schema provides a single 64-bit `address` field and mandates that `reserved` be zero (enforced by `deserialize_trace_event` rejecting non-zero reserved fields with `InvalidReservedField`).

**Design Determination**:
- When MMU translation is active (Stage 1 or Stage 2 enabled), `address` records the **Guest Virtual Address (VA)**, `flags` sets `HasVirtualAddress`, and `address_space` is `TraceAddressSpace::Virtual`.
- When MMU translation is inactive (bare-metal boot / MMU disabled), `address` records the **Guest Physical Address (PA)**, `flags` sets `HasPhysicalAddress`, and `address_space` is `TraceAddressSpace::Physical`.
- Storing both addresses simultaneously in the same event record without altering the 64-byte binary serialization format is not possible. This is documented as a known schema design characteristic.

---

## 5. Event Ordering Invariants

For any instruction performing guest memory access:

1. **Successful Single Access (e.g. `LDR X0, [X1]`, `STR W0, [X1]`)**:
   - Access completes successfully.
   - Event $N$: `MemoryRead` or `MemoryWrite`.
   - Event $N+1$: `InstructionRetired`.

2. **Successful Multi-Access (e.g. `LDP X0, X1, [X2]`, `STP W0, W1, [X2]`, 128-bit `LDR Q0, [X1]`)**:
   - Element 1 completes: Event $N$ (`MemoryRead` or `MemoryWrite` for element 1).
   - Element 2 completes: Event $N+1$ (`MemoryRead` or `MemoryWrite` for element 2).
   - Instruction completes: Event $N+2$ (`InstructionRetired`).

3. **Atomic Read-Modify-Write (e.g. `LDADD X0, X1, [X2]`, `SWP W0, W1, [X2]`)**:
   - Atomic read completes: Event $N$ (`MemoryRead`, `IsAtomic`, `data = old_value`).
   - Atomic write completes: Event $N+1$ (`MemoryWrite`, `IsAtomic`, `data = new_value`).
   - Instruction completes: Event $N+2$ (`InstructionRetired`).

4. **Failed Exclusive Store (e.g. `STXR W2, X0, [X1]` with lost reservation)**:
   - Reservation check fails: No memory write performed.
   - Status register updated to 1.
   - Zero `MemoryRead` events emitted.
   - Zero `MemoryWrite` events emitted.
   - Event $N$: `InstructionRetired`.

5. **Faulting Access (Translation / Permission / Alignment / Watchpoint Abort)**:
   - Fault detected during translation or alignment check.
   - `SynchronousExceptionSignal` thrown.
   - Zero `MemoryRead` events emitted.
   - Zero `MemoryWrite` events emitted.
   - Zero `InstructionRetired` events emitted.
   - Event $N$: `ExceptionEntered` (at vector entry with ESR and FAR).

---

## 6. Audit Conclusion & Action Plan

The memory access architecture is completely characterized. Instrumentation will be cleanly positioned within `CheckedGuestMemory` and `CPU`:
1. Add `emit_memory_read_trace` and `emit_memory_write_trace` in `CPU`.
2. Connect `CheckedGuestMemory::read` and `write` to emit events upon successful access completion.
3. Use an untraced read helper for internal exclusive monitor comparisons in `STXR`/`STXP` to prevent false `MemoryRead` emissions.
4. Verify bit-exact equivalence and full unit/integration test coverage.
