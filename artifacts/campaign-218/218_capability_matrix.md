# WinIOS Campaign 218 — Capability & Level Matrix

## 1. Capability Level Definitions

To maintain technical precision and honesty regarding the research profile, capabilities are classified into five distinct levels:

- **Level 1 (Host Simulation):** Pure host-side C++ logic and mocks executing on the Windows host without guest CPU involvement.
- **Level 2 (Synthetic Compatibility / Thunking):** Host structures exposed to guest ABI via synthetic shims without full architectural hardware isolation.
- **Level 3 (Freestanding Guest Execution):** Real ARM64 guest machine instructions compiled by cross-compiler, loaded into virtual address space, and stepped on the virtual CPU engine with MMU translation.
- **Level 4 (Architectural Hardware Emulation):** Full EL0/EL1 exception vector routing, system registers, physical page tables, and hardware MMU translation matching the target profile (iPhone 15, A16 Bionic T8120).
- **Level 5 (Production Darwin / iOS Environment):** Complete dynamic linking (dyld), libSystem, Foundation, CoreAnimation, UIKit, SpringBoard, and Metal.

---

## 2. Technical Capability Audit & Assessment

| Component / Subsystem | Campaign 217 Claimed | Campaign 218 Verified Reality | Verified Level |
| :--- | :--- | :--- | :---: |
| **Instruction Retirement** | "Retired instructions incremented" | Host integer increment -> Replaced with `ExecutionBoundaryEnforcer` counting real CPU steps | **Level 3** |
| **Mach-O Loader** | Freestanding Mach-O loader | Hardened for 16KB alignment, W^X enforcement, segment overlap detection, bounds checks | **Level 3** |
| **ARM64 Execution** | Real guest application execution | Compiler-generated Mach-O executes recursion, arithmetic, stack framing on `CPU` | **Level 3** |
| **Memory Isolation** | User address space | 16 KB translation granule, 32 GiB user address space limit, strict W^X permissions | **Level 4** |
| **Syscall Interface** | ABI v1.2 Category C syscalls | SYS 513–521 implemented in `UserSyscallDispatcher` with copyin/copyout and path safety | **Level 2 / 3** |
| **Dynamic Linking (dyld)**| Not implemented | Static PC-relative resolution only; dynamic Mach-O commands rejected | **Level 1** |
| **libSystem / Darwin CRT**| Mini freestanding CRT | Freestanding C runtime only (no Darwin POSIX libc) | **Level 2** |
| **UIKit / SwiftUI** | Not implemented | None. Research platform with custom virtual desktop rendering | **N/A** |
| **Metal / Host GPU** | Host presentation surface | Custom software rasterizer / presentation surfaces (not Metal) | **Level 2** |

---

## 3. Scope Boundaries & Non-Claims

WinIOS is a Windows-hosted research and emulation environment for the ARM64 Darwin architecture.
- WinIOS does **NOT** run production iOS, UIKit, SwiftUI, or SpringBoard.
- WinIOS does **NOT** include Apple proprietary binaries or bypass security enclaves.
- WinIOS demonstrates genuine 64-bit ARM64 Mach-O execution, 16 KB MMU translation, and Darwin-like ABI syscall isolation on Windows 11 x64.
