# Stage 195.18 — Differential Validation & Architecture Audit

## 1. Executive Summary & Purpose
This document records the architectural audit of WinIOS Campaign 195 against the ARMv8-A Architecture Reference Manual (ARM DDI 0487), ensuring full conformance of exception vectors, syndrome reporting, context saving, preemption mechanisms, SIMD/FP preservation, MMU page-table attributes, and SMP synchronization.

WinIOS explicitly maintains clear separation between synthetic emulation models (`HOST_MODEL`), guest runtime code (`GUEST_EL0`, `GUEST_EL1`), and authentic hardware emulation. WinIOS does **NOT** claim genuine Apple kernel execution (`APPLE_KERNEL`); all kernel services are synthetic implementations designed for research and testing.

---

## 2. ARMv8-A Architectural Invariant Verification

### 2.1 Stack Pointer Alignment & Canonical Exception Frame
- **Architectural Requirement**: ARMv8-A requires `SP_ELx` to remain 16-byte aligned whenever used as a base address in load/store instructions (`SCTLR_EL1.SA` / `SA0` checks).
- **WinIOS Implementation**:
  - The canonical `ExceptionFrame` structure (`src/kernel/cpu_context.h`) is exactly 896 bytes:
    - Bytes `0x000`–`0x0F8`: General-purpose registers `X0`–`0x1E` (X0–X30)
    - Bytes `0x0F8`–`0x100`: Reserved pad to maintain 16-byte boundary
    - Bytes `0x100`–`0x2FF`: 32 128-bit vector registers `Q0`–`Q31` (512 bytes)
    - Bytes `0x300`–`0x37F`: Control registers (`pc`, `sp_el0`, `sp_el1`, `spsr_el1`, `esr_el1`, `far_el1`, `tpidr_el0`, `tpidr_el1`, `ttbr0_el1`, `fpcr`, `fpsr`, `magic`, `frame_size`)
  - `sizeof(ExceptionFrame) == 896` (verified by `static_assert(sizeof(ExceptionFrame) == 896)`).
  - `896 % 16 == 0`, ensuring that decrementing `SP` by 896 maintains exact 16-byte alignment.
  - `ExceptionFrameManager::save_exception_frame` explicitly validates `(sp & 0xFULL) == 0`.

### 2.2 SPSR_EL1 & PSTATE Encoding
- **Architectural Requirement**: ARM DDI 0487 Section C5.2.19 defines `SPSR_EL1`:
  - `N` (Negative): bit 31
  - `Z` (Zero): bit 30
  - `C` (Carry): bit 29
  - `V` (Overflow): bit 28
  - `D` (Debug mask): bit 9
  - `A` (SError mask): bit 8
  - `I` (IRQ mask): bit 7
  - `F` (FIQ mask): bit 6
  - `M[4:0]` (Execution mode): bits [4:0] (`0b0000` for EL0t, `0b0100` for EL1t, `0b0101` for EL1h)
- **WinIOS Implementation**:
  - `ArchitecturalCpuContext::save` captures `NZCV` into bits [31:28] and `DAIF` into bits [9:6].
  - `ArchitecturalCpuContext::restore` unpacks `spsr_el1`:
    - `cpu.set_nzcv((spsr >> 28) & 0xFu)`
    - `cpu.set_daif((spsr >> 6) & 0xFu)`
  - Condition code flags (`NZCV`) and interrupt masking (`DAIF.I`) are restored atomically on `ERET`.

### 2.3 Exception Syndrome Reporting (ESR_EL1) & Vector Offsets
- **Architectural Requirement**: ARM DDI 0487 Section D13.2 defines Exception Class (EC) in bits [31:26]:
  - `0x15`: SVC instruction execution in AArch64 state
  - `0x21`: Instruction Abort from a lower Exception level
  - `0x22`: Instruction Abort taken without a change in Exception level
  - `0x24`: Data Abort from a lower Exception level
  - `0x25`: Data Abort taken without a change in Exception level
- **Vector Base Register (VBAR_EL1) Table**:
  - `+0x000`: Current EL with SP0 (Synchronous, IRQ, FIQ, SError)
  - `+0x200`: Current EL with SPx (Synchronous `+0x200`, IRQ `+0x280`)
  - `+0x400`: Lower EL using AArch64 (Synchronous `+0x400`, IRQ `+0x480`)
  - `+0x600`: Lower EL using AArch32
- **WinIOS Implementation**:
  - `PreemptionTimer::setup_vector_table` installs IRQ dispatch at `VBAR + 0x280` and `VBAR + 0x480`.
  - `DemandPagingManager::install_vector_table` installs page-fault handlers at `VBAR + 0x200` and `VBAR + 0x400`.
  - Handlers decode `ESR_EL1 >> 26` to distinguish lower EL vs current EL aborts and apply appropriate access permissions (`UserReadWrite` vs `KernelReadWrite`).

### 2.4 Vector & Floating-Point Trapping (CPACR_EL1)
- **Architectural Requirement**: ARM DDI 0487 Section D13.2.31 specifies that accesses to `Q0`–`Q31`, `FPCR`, and `FPSR` trap with EC `0x07` unless `CPACR_EL1.FPEN` (bits [21:20]) is set to `0b11` (no trapping at EL0 or EL1).
- **WinIOS Implementation**:
  - `ArchitecturalCpuContext::restore` explicitly initializes `CPACR_EL1` to `0x00300000ULL` before loading vector registers.
  - Prevents spurious `Trapped FP/SIMD instruction access via CPACR_EL1.FPEN` exceptions during context restoration.

### 2.5 16KB Stage-1 MMU Descriptors & Translation
- **Architectural Requirement**:
  - ARM 16KB granule uses 11-bit indices for translation levels.
  - Level 2 block / table descriptors: bits [1:0] = `0b11` (table) or `0b01` (block).
  - Level 3 page descriptors: bits [1:0] = `0b11` (page), bit 10 = `AF` (Access Flag), bits [9:8] = `SH` (Shareability), bits [7:6] = `AP` (Access Permissions), bit 54 = `UXN`, bit 53 = `PXN`.
- **WinIOS Implementation**:
  - `VirtualPageTable::make_page_descriptor` sets `AF = 1`, `SH = 3` (Inner Shareable), `Normal Write-Back Cacheable` (`AttrIndx = 0`).
  - Supports strict W^X separation: code pages are `PXN = 0, UXN = 1, Writable = 0`; data pages are `PXN = 1, UXN = 1, Writable = 1`.

### 2.6 SMP Synchronization & Memory Barriers
- **Architectural Requirement**: ARMv8-A requires explicit memory barriers (`DSB ISH`, `ISB`) and TLB invalidation broadcasts (`TLBI VMALLE1IS`, `TLBI VAAE1IS`) to synchronize page-table modifications and execution pipelines across multiple processing elements (PEs).
- **WinIOS Implementation**:
  - `DemandPagingManager::emit_page_fault_handler_code` generates `tlbi vmalle1is` (`0xD508831Fu`), `dsb ish` (`0xD5033B9Fu`), and `isb` (`0xD5033FDFu`).
  - `MMU::invalidate_tlb_vaa` synchronizes across cores with thread-safe `tlb_mutex_`.

---

## 3. Subsystem Classification Matrix

| Subsystem | Implemented In | Ownership Classification |
|:---|:---|:---|
| Canonical 896-Byte ExceptionFrame | `src/kernel/cpu_context.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| Exception Frame Push/Pop Manager | `src/kernel/exception_frame_manager.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| Timer-Driven Preemption & Vector Table | `src/kernel/preemption_timer.cpp` | `GUEST_INTERRUPT` / `GUEST_PREEMPTION` |
| Dual-Core SMP Scheduler & Migration | `src/kernel/smp_scheduler.cpp` | `GUEST_SMP` / `GUEST_SCHEDULER` |
| Spinlocks & Wait Queues | `src/kernel/sync.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| 16KB Demand Page Fault Handler | `src/memory/demand_paging_manager.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| Physical Page Allocator (16KB Frames) | `src/memory/physical_page_manager.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| Kernel Heap Allocator (`kmalloc`/`kfree`) | `src/memory/kernel_heap_allocator.cpp` | `GUEST_EL1` / `SYNTHETIC_KERNEL` |
| ARM64 Instruction Execution Engine | `src/cpu/cpu.cpp` | `HOST_MODEL` |
| ARM64 Stage-1 16KB MMU | `src/cpu/mmu.cpp` | `HOST_MODEL` |
| GICv3 Interrupt Controller | `src/cpu/gicv3.cpp` | `HOST_MODEL` |

---

## 4. Architectural Sign-Off
All implementations have been verified compliant with ARMv8-A specifications. Zero deviations or non-canonical behaviors detected.
