# WinIOS Campaign 221 — Stage 221.1 Forensic Audit & ABI Naming Correction

**Date:** 2026-11-18  
**Campaign:** 221  
**Audit Target:** Campaign 220 Technical Claims, Architectural State, and ABI Definitions  

---

## 1. Architectural Register Context Analysis

### 1.1 The "34-Register Context" Resolution
In Campaign 220 documentation, the CPU context was frequently summarized as a "34-register context". A detailed inspection of `winios::kernel::GuestTcb` and `winios::kernel::KernelThread` reveals the exact breakdown:

1. **General Purpose Registers (31):**
   - `X0` through `X30` (248 bytes in `GuestTcb::gpr[31]`).
   - `X29` acts as Frame Pointer (FP).
   - `X30` acts as Link Register (LR).
2. **Stack Pointers (2):**
   - `SP_EL0` (`tcb_.sp_el0`): EL0 User Stack Pointer.
   - `SP_EL1` (`tcb_.sp_el1`): EL1 Kernel Stack Pointer.
3. **Control Registers (2):**
   - `PC` (`tcb_.pc`): Program Counter.
   - `SPSR_EL1` (`tcb_.spsr_el1`): Saved Process Status Register (holds EL0/EL1 mode bits and NZCV condition flags).
4. **Thread-Local Storage (1):**
   - `TPIDR_EL0` (`tcb_.tpidr_el0`): Thread ID / TLS base pointer.
5. **Floating-Point & SIMD State:**
   - Vector Registers (`v0`–`v31`): 32x 128-bit vector registers stored in `KernelThread::fp_regs_` on the host side.
   - `FPCR` / `FPSR`: Maintained inside the virtual CPU core sysreg state. In Campaign 220, `GuestTcb` reserved 108 bytes (`reserved[108]`), leaving space for explicit guest serialization of FPCR/FPSR.

**Summary:** The core integer context consists of 35 distinct architectural fields (31 GPRs + SP_EL0 + SP_EL1 + PC + SPSR_EL1), plus TPIDR_EL0. Including the 32 SIMD registers yields a 68-field total architectural context. The term "34-register context" historically omitted SP_EL1 or TPIDR_EL0.

---

## 2. Address Space & ASID Allocation Mechanics

### 2.1 ASID Allocation & Reuse
- **Range:** 1 to 255 (`kMaxAsid = 255`). ASID 0 is reserved for the kernel/idle context.
- **Allocation:** `BsdProcessManager::allocate_asid()` increments sequentially.
- **Limitation Identified in Audit:** When 255 processes have spawned over runtime lifetime, ASIDs do not currently recycle freed IDs from terminated processes. In Campaign 221, an ASID bitmap allocator must be implemented to support reuse.
- **TLB Tagging & Invalidation:** `VirtualPageTable::flush_asid(asid)` clears all TLB cache entries associated with the terminated process's ASID. In addition, `cpu.flush_tlb()` is called when switching address spaces.

---

## 3. Instruction Retirement Provenance

- **Tracking Mechanism:** `RecordGuestInstructionRetirementExplicit(count, pid, tid, pc)` increments:
  1. `KernelThread::tcb_.instructions_retired`
  2. `BsdProcess::metrics_.instructions_retired`
  3. `UserAddressSpace::metrics_.instructions_retired`
- **Provenance Verification:** Tests must verify that retirement occurs during `cpu.step(mem)`. Host-side synthetic event loops that do not execute ARM64 instructions must NOT increment these guest retirement metrics.

---

## 4. Syscall Classification & ABI Terminology Correction

Campaign 220 referred broadly to "Darwin ABI". To ensure technical precision and avoid misleading claims, all interfaces are formally classified into the following ABI categories:

### Table 4.1: WinIOS ABI Classification Matrix

| Category | Range / Identifier | Official Designation | Semantics & Conformance |
|---|---|---|---|
| **Category A** | `SYS_0` .. `SYS_499` | **WinIOS Darwin-Compatible BSD ABI** | Clean-room implementation of standard Darwin BSD syscall numbers (e.g. `SYS_exit`=1, `SYS_read`=3, `SYS_write`=4, `SYS_close`=6, `SYS_pipe`=42, `SYS_getpid`=20, `SYS_wait4`=7). Positive errno returned in X0 with C-flag set in `DarwinStrict` mode. |
| **Category B** | `0x80000001` (`kSysWiniosMsgTrap`) | **WinIOS Mach-Inspired Message Trap** | Bounded message passing across IPC ports. Distinct from XNU's full Mach IPC subsystem. |
| **Category C** | `SYS_500` .. `SYS_599` | **WinIOS Guest Accelerated ABI** | Custom WinIOS platform extensions for desktop windowing, graphics surfaces, isolated storage, event loops, and timers. Designated as **WinIOS Guest ABI v1.4** (and v1.5). |

**Directive:** Do not refer to the combined ABI as "Apple Darwin ABI" or "iOS ABI". Use the exact terms:
- `WinIOS Guest ABI v1.4` (Campaign 220 baseline)
- `WinIOS Guest ABI v1.5` (Campaign 221 enhancements)

---

## 5. Audit Acceptance

All ambiguities from Campaign 220 regarding register context counts, ASID allocation, retirement provenance, and ABI naming have been formally audited, clarified, and codified for Campaign 221.
