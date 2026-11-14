# Campaign 195 — Architecture Decisions Record (ADR)

## ADR-195-01: Canonical 896-Byte ExceptionFrame Structure
- **Context**: In asynchronous preemptive multitasking, interrupting a guest thread at an arbitrary instruction boundary requires preserving the *entire* architectural CPU context, not just callee-saved registers.
- **Decision**: Define a canonical 896-byte `ExceptionFrame` structure (`src/kernel/cpu_context.h`):
  - 248 bytes for X0–X30
  - 8 bytes reserved padding (maintaining 16-byte alignment)
  - 512 bytes for 32 128-bit vector registers Q0–Q31
  - 128 bytes for control words (PC, SP_EL0, SP_EL1, SPSR_EL1, ESR_EL1, FAR_EL1, TPIDR_EL0/EL1, TTBR0_EL1, FPCR, FPSR, magic, size)
- **Consequence**: The frame occupies exactly 896 bytes, which is a multiple of 16 bytes (`896 % 16 == 0`), strictly obeying the ARMv8-A stack alignment invariant (`SP % 16 == 0`).

---

## ADR-195-02: Initial Thread Dispatch vs Preempted ExceptionFrame Detection
- **Context**: Threads created via `create_thread` begin with an uninitialized or empty stack; they do not have a pre-pushed `ExceptionFrame`. Preempted threads, however, have an 896-byte frame pushed to the stack top.
- **Decision**: In `ExceptionFrameManager::restore_exception_frame`:
  - Inspect the memory at `thread->stack_pointer()`. If the header matches `kExceptionFrameMagic` (`0x455843504652414D`), unpack the `ExceptionFrame`.
  - If magic is missing (initial launch), fall back to `thread->load_context_to_cpu(cpu)`, loading entry point and initial registers from the `GuestTcb`.
- **Consequence**: Seamless dispatch of both newly spawned threads and asynchronously preempted threads without requiring synthetic boot frames.

---

## ADR-195-03: CPACR_EL1.FPEN Initialization Prior to Vector Register Access
- **Context**: In ARMv8-A, floating-point and Advanced SIMD instructions trap to EL1 if `CPACR_EL1.FPEN` (bits [21:20]) is `0b00` or `0b01`.
- **Decision**: `ArchitecturalCpuContext::restore` explicitly sets `cpu.set_cpacr_el1(0x00300000ULL)` *before* accessing `Q0`–`Q31`, `FPCR`, or `FPSR`.
- **Consequence**: Eliminates spurious vector trapping exceptions during context restores across threads.

---

## ADR-195-04: Bidirectional Kernel Heap Coalescing
- **Context**: Forward-only coalescing in `kfree` leads to artificial fragmentation when blocks are freed out-of-order (e.g. freeing B, D, then middle block C).
- **Decision**: Enhance `KernelHeapAllocator::kfree` to perform both backward coalescing (scanning preceding contiguous free blocks) and forward coalescing (looping over adjacent free blocks).
- **Consequence**: Adjacent free blocks are unified into the maximum contiguous free span, preventing false out-of-memory errors on large allocations.

---

## ADR-195-05: Strict Boundary Rejection & Double-Free Protection in Physical Allocator
- **Context**: Allocator stress testing demonstrated that freeing an already-freed page must not corrupt free-page counters or bitmap tracking.
- **Decision**: `PhysicalPageManager::free_pages` validates `get_bit(pfn)` for all pages in the range before clearing bits. If any page is already free, the operation returns `false` immediately.
- **Consequence**: Prevents memory accounting drift, double-allocation hazards, and bitmap corruption.
