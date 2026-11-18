# WinIOS Campaign 219 — Stage 219.0 Baseline & Integrity Report

## 1. Baseline Verification

- **Repository Root:** `D:\WinISO26\WinIOS\`
- **Branch:** `campaign-191r`
- **Starting Git HEAD:** `28ee0df5928e897a7fb142abe317bdc98e33057c`
- **Expected Baseline:** Campaign 218 closeout commit (`28ee0df5928e897a7fb142abe317bdc98e33057c`)
- **Status:** **MATCH CONFIRMED** — Working tree clean, HEAD strictly aligned.

---

## 2. Protected Paths & Integrity Check

All four non-negotiable protected paths were audited and verified bit-for-bit:

| Path | Required State / SHA-256 | Observed Verification | Result |
| :--- | :--- | :--- | :---: |
| `D:\WinISO26\WinISO26_README.md` | Must NOT exist | File does not exist | **PASS** |
| `D:\WinISO26\WinIOS_README.md` | `61F4D078D5071B5AB16093EB5418ED15AFB7ED41CD3C84F080CE44E196EA7FEF` | SHA-256 Matches | **PASS** |
| `D:\WinISO26\WinIOS\docs\progress.md` | `447E7B5AB74B9331CBE618BEA6561ABA83C4D291DC5CCC16DF1D72023C462212` | SHA-256 Matches | **PASS** |
| `D:\WinISO26\Inferno-Research\` | Must remain unmodified | Checkout clean and unmodified | **PASS** |

---

## 3. Initial Build & CTest Inventory

- **Toolchain:** MSVC C++20 (`/W4 /WX /permissive-`), CMake 3.31.6, CTest, Zig 0.17.0
- **Registered CTest Suites:** 753 suites
- **Campaign 218 Baseline Test:** `stage218_22_native_guest_e2e_tests` executed and **PASSED (100%)**.

---

## 4. Current Architectural State & Inventory

- **Process Model:** `BsdProcess` and `BsdProcessManager` exist, tracking state, PID, PPID, and FD table. Process spawner in `src/kernel/process_spawner.h` coordinates EL0 launch.
- **Address Space:** `UserAddressSpace` supports Stage-1 page tables (16 KB translation granule) up to 32 GiB (`0x800000000ULL`) with strict W^X permissions.
- **CPU & Registers:** `CPU` class with full general-purpose registers (X0–X30, SP_EL0, PC, PSTATE), architectural exception delivery, MMU translation, and sysregs (`TPIDR_EL0`, `SCTLR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `MAIR_EL1`).
- **Execution Boundary:** `ExecutionBoundaryEnforcer` prevents host simulation counters from inflating verified guest instruction counts.
- **Syscall Dispatcher:** `UserSyscallDispatcher` handles standard Darwin POSIX syscalls and WinIOS Category C extensions (500–521).
- **VFS & Storage:** `VirtualFileSystem` and `IsolatedStorage` provide hierarchical rootfs and sandboxed application storage.
- **IPC & Events:** `GuestIpcRouter` and `GuestEventServices` provide port-based mailboxes and virtual timers.
- **Loader:** `MachoUserLoader` loads 64-bit ARM64 Mach-O binaries, enforces 16 KB alignment, segment overlap checks, and W^X.

---

## 5. Identified Limitations for Campaign 219

1. **Multi-Process Scheduling:** `MultiProcessScheduler` is currently cooperative/round-robin across tasks, but lacks multi-thread context preemption/switching within a single process.
2. **Multi-Threading:** Each `BsdProcess` holds a list of `KernelThread*`, but threads do not currently maintain independent user CPU register contexts and stacks during concurrent guest execution.
3. **Thread-Local Storage (TLS):** `TPIDR_EL0` exists in `CPU`, but per-thread TLS allocation and compiler-generated TLS relocations/access patterns have not been validated end-to-end.
4. **Synchronization:** Guest-visible futex/mutex syscall interfaces are absent; synchronization has previously relied on host abstractions.
5. **VFS File Descriptors & Pipes:** `FileDescriptorTable` supports basic descriptors, but genuine pipe IPC between two running guest processes via file descriptors needs full integration.
6. **Dynamic Linking:** Loader is currently static-only; `LC_LOAD_DYLIB` and `LC_LOAD_DYLINKER` need audit and bounded feasibility assessment.
