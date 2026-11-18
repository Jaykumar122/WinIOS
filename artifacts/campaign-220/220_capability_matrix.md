# WinIOS Campaign 220 — Capability Matrix
## Execution Conformance, Process/Thread Hardening & ABI Validation

**Campaign:** 220  
**Baseline Hash:** `8f00af545488e9641070b4ac2bdeb7c6374aca75`  
**Host:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Cross Compiler:** Zig 0.17.0 (`zig cc -target aarch64-macos -nostdlib`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`), ARM64, 16 KB Granule

---

### 1. Architectural Capability Level Classification

In strict adherence to the project's engineering integrity rules, all capabilities are classified into precise Technical Readiness Levels (Levels 1 through 5):

| Domain | Capability Level | Functional Status | Verification Scope |
|---|---|---|---|
| **ARM64 CPU Core** | **Level 5: Production Conformance** | Genuine instruction execution, register bank preservation (X0-X30, SP_EL0/EL1, PC, PSTATE, NZCV, V0-V31), hardware single-stepping. | Tests #767–#772, #783–#786 |
| **MMU & Virtual Memory** | **Level 5: Production Conformance** | 16 KB translation granule, 2-level page tables (L2/L3), TCR_EL1/MAIR_EL1 configuration, 16-bit hardware ASID tagging, non-global (nG) page translations, TLB invalidation. | Tests #768, #769 |
| **Execution Provenance** | **Level 5: Production Conformance** | Unforgeable ring buffer tracking origin tags (`kGuestCpuRetired`, `kHostCallback`, `kSimulationFallback`), rejection of unverified increments. | Tests #767, #783–#786 |
| **Process Management** | **Level 4: High-Fidelity Subsystem** | Independent virtual address spaces, ASID assignment, PID allocation (0-63), parent-child process tree, state machine (`Starting` -> `Runnable` -> `Zombie`), orphan reparenting to PID 1, wait4 status collection. | Tests #768, #776, #785 |
| **Thread Architecture** | **Level 4: High-Fidelity Subsystem** | 512-byte aligned `GuestTcb`, complete AArch64 context switching (X0-X30, V0-V31, TPIDR_EL0, SP_EL0), user stack allocation & alignment, TID tracking. | Tests #770, #771, #773, #775, #784 |
| **Guest Scheduler** | **Level 4: High-Fidelity Subsystem** | Deterministic round-robin scheduling, fairness guarantees, quantum tracking, distinct same-process vs. cross-process context switch accounting, zero lost updates. | Tests #772, #773, #784, #786 |
| **Guest ABI v1.4** | **Level 4: High-Fidelity Subsystem** | Unified Darwin-Strict (X16 syscall number, X0-X5 args, Carry bit errno) and Fast ABI modes, Category C memory protections, pointer bounds validation, wrap-around overflow defenses. | Tests #774, #777, #780 |
| **VFS & File Descriptors** | **Level 3: Functional Emulation** | Standard 64-descriptor table, clone-on-fork semantics, circular `Pipe` FIFO (16 KB) with EOF and `EPIPE` signaling, non-blocking backpressure. | Tests #778, #779, #786 |
| **IPC & Port Messaging** | **Level 3: Functional Emulation** | Mach-inspired message traps (`SYS_winios_msg_trap` / `SYS_mach_msg`), port capability ownership, 4 KB payload limit, 32-message queue depth. | Tests #780, #785 |
| **Service Management** | **Level 3: Functional Emulation** | Launch-on-demand daemon architecture, port rendezvous, crash detection, automatic service restart bounded by max restart limits. | Tests #781, #785 |
| **Dynamic Loading** | **Level 3: Functional Emulation** | Mach-O LC_LOAD_DYLIB parser, atomic rollback on allocation failure, dylib deduplication/idempotency, symbol resolution. | Tests #782, #785 |
| **High-Level Frameworks** | **Level 0: Explicitly Non-Existent** | No production Apple iOS, UIKit, SwiftUI, CoreAnimation, or Metal compatibility is claimed or implied. All functionality is research-grade freestanding execution. | By Architecture Policy |

---

### 2. Syscall Dispatch Verification (Darwin Strict & WinIOS ABI v1.4)

| Syscall Number | Name | Darwin ABI Mapping | Tested Behavior | Status |
|---|---|---|---|---|
| 1 | `SYS_exit` | X16 = 1, X0 = status | Terminate process/thread, transition to Zombie | VERIFIED |
| 3 | `SYS_read` | X16 = 3, X0 = fd, X1 = buf, X2 = nbyte | Read from VFS or Pipe with pointer safety | VERIFIED |
| 4 | `SYS_write` | X16 = 4, X0 = fd, X1 = buf, X2 = nbyte | Write to VFS, Pipe, or Console with bounds | VERIFIED |
| 7 | `SYS_wait4` | X16 = 7, X0 = pid, X1 = status, X2 = opt | Reap child process, extract exit code | VERIFIED |
| 520 | `SYS_winios_thread_create` | X16 = 520, X0 = entry, X1 = arg, X2 = sp | Create new guest thread with validated stack | VERIFIED |
| 521 | `SYS_winios_thread_exit` | X16 = 521, X0 = exit_code | Terminate thread, release CPU quantum | VERIFIED |
| 522 | `SYS_winios_thread_join` | X16 = 522, X0 = tid, X1 = status_va | Join target thread, write exit status | VERIFIED |
| 524 | `SYS_winios_mutex_lock` | X16 = 524, X0 = mutex_va | Atomic lock with deadlock detection | VERIFIED |
| 525 | `SYS_winios_mutex_unlock` | X16 = 525, X0 = mutex_va | Release lock, enforce caller ownership | VERIFIED |
| 527 | `SYS_winios_msg_trap` | X16 = 527, X0 = msg_buf_va | Dispatch Mach-inspired message to port | VERIFIED |
