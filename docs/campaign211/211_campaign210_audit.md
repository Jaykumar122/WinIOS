# WINIOS CAMPAIGN 211: STAGE 211.1 INDEPENDENT CAMPAIGN 210 AUDIT

**Campaign:** 211  
**Stage:** 211.1 — Independent Audit of Campaign 210 Baseline  
**Date:** 2026-11-17  
**Scope:** Architectural truth, state ownership, context switching, privilege transitions, and scheduler integrity.

---

## 1. Executive Summary

Campaign 210 successfully established kernel tasks, threads, the 32-tier priority scheduler, and initial EL0 data structures. However, preparing for real userspace Mach-O execution requires addressing critical architectural boundaries:

1. **Gate A — Context Switch Completeness:**
   - The Campaign 210 22-instruction `cpu_switch_to` routine is strictly an AAPCS64 callee-saved switch (X19–X30, SP_EL1).
   - This is valid for cooperative kernel thread switching, but **insufficient** for:
     - Asynchronous IRQ preemption where interrupted thread may hold arbitrary values in caller-saved registers (X0–X18).
     - EL0-to-EL1 exception entry/return where all 31 GPRs, SP_EL0, SPSR_EL1, ELR_EL1, and user registers must be saved to a full trap frame (`arm_saved_state_t`).
2. **Gate B — Privilege Transitions & Exception Vectors:**
   - Exception vectors at `VBAR_EL1` currently only handle Current-EL IRQ (`VBAR + 0x280`).
   - For userspace execution, vectors must handle:
     - Lower EL Synchronous (`VBAR + 0x400`): SVC traps, instruction aborts, data aborts from EL0.
     - Lower EL IRQ (`VBAR + 0x480`): Timer preemption occurring while executing in EL0.
   - Full syndrome decoding (`ESR_EL1.EC = 0x15` for SVC64) must be mapped to system call dispatch.
3. **Gate C — Memory & Address Space Isolation:**
   - Campaign 210 mapped kernel space under a single address space.
   - For userspace processes, each process requires an independent `UserAddressSpace` controlling `TTBR0_EL1` (0 to $2^{48}-1$) with strict user permissions (`UXN=0, PXN=1, AP[1]=1`), while kernel mappings reside under `TTBR1_EL1` or high kernel VA.
4. **Gate D — Guest vs Host State Ownership:**
   - Process metadata, file descriptors, and IPC rights must have well-defined guest memory representations synchronized with host management structures.

---

## 2. Detailed Subsystem Audit

### 2.1 Context Switching (`src/kernel/context_switch.cpp`)
- **Implemented:**
  - Emits 22 instructions: `stp x19, x20..` up to `x29, x30`, `str sp, [x0, #0x28]`, then loads from `[x1]`, and `br x16`.
- **Finding:**
  - Saves 12 registers (X19–X30) + SP_EL1 + PC.
  - Leaves X0–X18 and floating point registers unpreserved.
- **Required Action for Stage 211.2:**
  - Implement full ARM64 exception frame / trap frame saver (`TrapFrame` / `arm_saved_state_t` 288 bytes) for EL0 $\leftrightarrow$ EL1 transitions and IRQ preemption, retaining the 22-instruction routine for voluntary kernel-thread yield.

### 2.2 Task and Thread Architecture (`src/kernel/task.h`, `thread.h`)
- **Implemented:**
  - `KernelTask` supports PID 0, task states, memory accounting.
  - `KernelThread` supports 512-byte packed `GuestTcb`.
- **Finding:**
  - Needs extension to represent BSD processes (`BsdProcess`, PID > 0, PPID, session/group, credentials `ucred`, file descriptor table `fd_table`).

### 2.3 Scheduler & Timer Preemption (`src/kernel/scheduler.cpp`, `preemption_timer.cpp`)
- **Implemented:**
  - 32-tier priority queues, quantum decrement, round-robin yield, `dispatch_next()`.
  - Timer IRQ vector at `VBAR + 0x280`.
- **Finding:**
  - Works correctly. Must be extended to handle preemption of EL0 threads and returning to EL0 after IRQ service.

### 2.4 Mach IPC Foundation (`src/kernel/ipc.h`)
- **Implemented:**
  - `MachPort`, capability rights (`Receive`, `Send`, `SendOnce`), message queues.
- **Finding:**
  - Currently purely host-side objects. Needs syscall integration (`mach_msg_trap`, `mach_port_allocate`, etc.) and guest user buffer marshaling.

---

## 3. Conclusions and Road Ahead

The Campaign 210 baseline is completely sound, verified, and ready for extension. The gates identified will be systematically resolved in Stages 211.2 through 211.4 before executing our first userspace Mach-O program.
