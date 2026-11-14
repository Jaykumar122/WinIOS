# Campaign 194 — Architecture Decisions

This document formalizes the major architectural decisions and engineering rationales implemented during Campaign 194.

---

## 1. 256-Byte Packed Boot Contract in Register X0 (`SyntheticKernelBootInfo`)
- **Decision:** Establish a strictly packed 256-byte boot record (`SyntheticKernelBootInfo`) passed to the kernel entry point in register `X0` at EL1.
- **Rationale:** Operating systems on ARM64 require explicit boot parameters (physical memory base/size, kernel image load address/size, page table root base, device tree / MMIO offsets, and boot flags). Packing this into a deterministic 256-byte contract with a 64-bit magic header (`0x4B45524E454C3031`) allows freestanding guest kernels to validate boot parameters using standard AAPCS64 register conventions without hardcoding host addresses.

---

## 2. 16KB-Granule Page Frame Allocator (`GuestPhysicalAllocator`)
- **Decision:** Allocate memory in uniform 16KB page frames tracked by a guest-side bitmap.
- **Rationale:** Apple Silicon architectures (A12 through A16 and M-series) utilize a native 16KB translation granule (`TCR_EL1.TG0 = 0b10`). Emulating a 16KB granule ensures that physical frame numbers (PFNs), page alignment checks, and page table index computations conform to genuine Darwin kernel expectations.

---

## 3. Two-Level Page Table Walking (L2 Table Descriptors and L3 Page Descriptors)
- **Decision:** Implement a hierarchical page table walk adhering to ARMv8-A 16KB translation format: L2 tables provide 32MB block entries or point to L3 tables; L3 tables provide individual 16KB page mappings.
- **Rationale:** Conforms directly to the Stage-1 MMU implementation in `mmu.cpp`. Bit 1 of table and block descriptors distinguishes block/table vs invalid entries, while bits [54:53] (`UXN`/`PXN`) and bits [7:6] (`AP[2:1]`) enforce memory protection and execute-never policies.

---

## 4. Callee-Saved Register Context Switching (`cpu_switch_to`)
- **Decision:** Perform cooperative CPU context switching by saving only AAPCS64 callee-saved registers (`X19-X28`, `FP (X29)`, `LR (X30)`, `SP_EL1`) onto the outgoing thread's kernel stack, and restoring them from the incoming thread's stack.
- **Rationale:** Full 32-register state preservation is only required for asynchronous interrupt preemptions and exceptions. Routine voluntary context switches (e.g. `yield`, blocking IPC) only require callee-saved registers per AAPCS64 ABI, cutting memory traffic and instruction count by over 60%.

---

## 5. 32-Priority Run Queues with $O(1)$ Bitmap Scheduling
- **Decision:** Structure the priority scheduler with 32 distinct priority levels (0 lowest, 31 highest) indexed by a 32-bit bitmap (`ready_bitmap_`). Finding the highest-priority runnable thread uses `__builtin_clz` / bit scan reverse (`31 - clz(ready_bitmap_)`).
- **Rationale:** Provides guaranteed $O(1)$ thread selection regardless of total thread count. Threads within the same priority level execute round-robin with configurable quantum slices (default 10 ms / timer ticks).

---

## 6. ARM64 Exclusive Spinlocks (`LDAXR` / `STLXR` / `STLR`)
- **Decision:** Implement low-level kernel spinlocks using ARM64 exclusive load-acquire (`LDAXR`) and store-release (`STLXR`), with unlock performed by store-release (`STLR WZR, [X0]`).
- **Rationale:** Conforms to Section B2.9 of ARM DDI 0487 for synchronization primitives. Enforces single-copy atomic visibility and hardware memory barriers across multiple guest execution contexts without host locking artifacts.

---

## 7. Dual Syscall Convention Support (X8 Standard and X16 Darwin Trap)
- **Decision:** Allow the syscall dispatcher to recognize system call numbers in both register `X8` (standard freestanding / Linux ABI) and register `X16` (Darwin BSD syscall / Mach trap ABI), passing return values in `X0`.
- **Rationale:** Ensures compatibility across freestanding synthetic guest kernel workloads and Darwin-linked userland binaries without requiring conflicting dispatcher paths.

---

## 8. AAPCS64 Stack Frame Unwinding with Termination Guarantees
- **Decision:** Implement `KernelDiagnostics::unwind_stack` enforcing strict 8-byte alignment, memory boundary limits, and strictly monotonic progression (`next_fp > current_fp`).
- **Rationale:** Corrupted, circular, or malicious frame pointer chains could induce infinite loops in host or guest crash reporters. Enforcing monotonic frame advancement guarantees termination within a bounded frame count (maximum 32 frames).
