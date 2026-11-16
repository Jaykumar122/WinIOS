# Stage 204.16: Thread Creation & Lifecycle Verification Report

## 1. Overview
Stage 204.16 implements the thread creation, state machine, and lifecycle architecture in `DarwinThreadRuntime` and `DarwinBsdSubsystem`. It provides:
1. Thread stack allocation with hardware-enforced MMU guard pages (`PROT_NONE`).
2. Conformance to Apple Silicon ARM64 thread-pointer register binding (`TPIDRRO_EL0` for `pthread_t` and `TPIDR_EL0` for TLS).
3. Thread state transitions (`Running`, `Suspended`, `Blocked`, `Terminated`).
4. Thread Local Variable (TLV) descriptor resolution.
5. Multi-thread tracking, accounting, and process-level isolation.

## 2. Key Implementations
1. **Stack Allocation with Guard Page:**
   - `allocate_thread_stack` maps a contiguous virtual address region divided into:
     - `guard_page`: `[stack_base, stack_base + guard_size)` with `PROT_NONE` (0).
     - `usable_stack`: `[stack_base + guard_size, stack_base + guard_size + stack_size)` with `PROT_READ | PROT_WRITE` (3).
   - Stack pointer `initial_sp` points to the top of the usable stack and is 16-byte aligned.
   - Guard page strictly rejects read/write access via `check_vm_access`, preventing stack overflow.

2. **Thread Control Block & Register Binding:**
   - Packed `DarwinPthreadHeader` (184 bytes) initialized with magic `PTHRD_T` (`0x50544852445F5400ULL`).
   - TSD slots 0..15 configured: Slot 0 points to self (`pthread_t`), Slot 1 points to `errno_val`.
   - `bind_cpu_thread` sets:
     - `TPIDRRO_EL0 = pthread_va` (read-only pointer to thread structure).
     - `TPIDR_EL0 = tls_base` (read/write pointer to user TLS).

3. **Thread State Machine:**
   - Tracks thread states (`Running`, `Suspended`, `Blocked`, `Terminated`).
   - State transition rules enforce strict POSIX pthread lifecycle invariants:
     - Only `Running` threads can be suspended or blocked.
     - Only `Suspended` threads can be resumed.
     - Only `Blocked` threads can be unblocked.
     - Terminated threads cannot transition to any active state.
   - Records thread exit codes upon termination.

4. **TLV Descriptor Resolution:**
   - Implements `resolve_tlv_address` supporting both Key 0 (relative to `TPIDR_EL0`) and Key 1 (relative to `TPIDRRO_EL0`).

## 3. Test Verification
- Target: `stage204_16_thread_lifecycle_tests`
- Release build & test: PASS (5/5 test suites, 0 failures)
- Debug build & test: PASS (5/5 test suites, 0 failures)
- Zero compiler warnings or errors (`/W4 /WX /permissive-`).
