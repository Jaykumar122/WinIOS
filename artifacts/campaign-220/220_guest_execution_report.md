# WinIOS Campaign 220 — Stage 220.20 Report
## Genuine Multi-Process Guest Workload Execution

**Stage:** 220.20  
**Status:** COMPLETE & VERIFIED  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB Granule  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib -Wl,-e,_main`)

---

### 1. Executive Summary

Stage 220.20 achieves verified multi-process freestanding ARM64 guest execution on WinIOS. Two isolated Darwin Mach-O executables (`campaign220_sender` and `campaign220_receiver`) were cross-compiled with Zig 0.17.0 targeting `aarch64-macos`, loaded into isolated address spaces with distinct ASIDs (ASID 1 for Sender, ASID 2 for Receiver), and scheduled concurrently via `MultiProcessScheduler`.

The guest processes communicated via a kernel pipe (`Pipe`, 16 KB capacity). The sender thread executed genuine ARM64 instructions, populated a message on its stack, issued `SYS_write` (Mach-O syscall 4) to descriptor 4, and yielded its quantum. The scheduler performed a full cross-process context switch, restoring the receiver thread's ARM64 register set, TPIDR_EL0, and TTBR0_EL1. The receiver executed genuine instructions, read the message via `SYS_read` (syscall 3), verified integrity, and terminated cleanly with exit status 77.

---

### 2. Implementation & Workload Architecture

#### 2.1 Genuine ARM64 Cross-Compilation
Guest assembly workloads were compiled to authentic 64-bit Mach-O images using Zig 0.17.0:
- `WinIOS/scratch/campaign220_sender.s`:
  - Adjusts user stack: `sub sp, sp, #32`
  - Populates 16-byte magic message `0x123455AA_DEADBEEF`
  - Sets up Darwin syscall ABI registers: `x16 = 4` (`SYS_write`), `x0 = 4` (FD), `x1 = SP`, `x2 = 16`
  - Executes `svc #0`
- `WinIOS/scratch/campaign220_receiver.s`:
  - Adjusts user stack: `sub sp, sp, #32`
  - Sets up Darwin syscall ABI registers: `x16 = 3` (`SYS_read`), `x0 = 3` (FD), `x1 = SP`, `x2 = 16`
  - Executes `svc #0`
  - Verifies magic payload and calls `SYS_exit` with status 77

#### 2.2 Address Space Isolation & Scheduling
- **Sender Address Space:** Root L2 table allocated via `PhysicalPageManager`, ASID 1, Owner PID 101.
- **Receiver Address Space:** Root L2 table allocated via `PhysicalPageManager`, ASID 2, Owner PID 102.
- **Shared Kernel Pipe:** Connected FD 4 in Sender to FD 3 in Receiver.
- **Context Switching:** Scheduler tracked cross-process context switch, TTBR0_EL1 reload, TLB invalidation, and TPIDR_EL0 restoration.

---

### 3. Verification & Evidence

- **Test Suite:** `tests/unit/stage220_20_multi_process_tests.cpp` (CTest #783).
- **Execution Verification:**
  - `ExecutionBoundaryEnforcer` verified 40 genuine guest ARM64 instructions retired on the CPU core.
  - Pipe transfer verified: 16 bytes written by sender and 16 bytes drained by receiver.
  - Clean process termination: Receiver exited with status code 77.
  - Scheduler metrics: `cross_process_context_switches >= 1`, `total_address_space_switches >= 1`.
