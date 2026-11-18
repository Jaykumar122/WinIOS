# WinIOS Campaign 227: Capability Matrix & Research Disclosures

## 1. Document Services & Session Capability Matrix

| Feature / Subsystem | Current Status | Enforcement Level | Architectural Mechanism |
|---|---|---|---|
| 64-Bit Document Generation Handles | **Production Ready** | Kernel Space | Monotonic ID (32-bit) + Generation Counter (32-bit) |
| Permitted Sandbox Roots | **Production Ready** | VFS / Kernel | Strictly `/Documents`, `/Shared`, `/Downloads`, `/tmp` |
| Directory Traversal Prevention | **Production Ready** | Kernel | Rejects `..`, `\\`, and non-normalized paths |
| Cross-Process Access Delegation | **Production Ready** | Kernel Space | Unique 64-bit grant tokens, capability-checked |
| Category C ABI v2.1 Syscalls (571..580)| **Production Ready** | EL0 Trap / SVC | ARM64 Darwin `svc #0x80`, register ABI, MSVC struct align |
| Atomic Stream Save | **Production Ready** | VFS / Kernel | Staged write to `.tmp` file + atomic promotion rename |
| Bounded Stream Quota (10 MB) | **Production Ready** | Stream Manager | Enforced on `write_stream` and `seek_stream` |
| Virtual File Picker Service | **Production Ready** | Compositor Owned | Out-of-process modal dialogue, async request tokens |
| Recent Documents Manager | **Production Ready** | User Space / VFS| Bounded 16-item LRU history with live VFS validation |
| Desktop Session Persistence | **Production Ready** | Compositor / VFS| Atomic state save to `/System/Library/Sessions/` |
| Freestanding ARM64 Workloads | **Production Ready** | Guest CPU Core | Zig 0.17.0 compiled Mach-O executables, verified retirement |

---

## 2. Research Environment Disclosures

1. **Host Platform:** Windows 11 x64 host with Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`) and CMake. No Linux, WSL, macOS, Xcode, or Apple hardware dependencies.
2. **Guest Execution:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) research profile executing in ARM64 EL0 with 16 KB translation granules and public pinned XNU `xnu-10002.1.13` syscall compatibility.
3. **UIKit / AppKit Emulation:** WinIOS provides a lightweight, Darwin-compatible, capability-bounded micro-runtime with software font rendering, window server, and document streams. It does NOT incorporate proprietary Apple frameworks, proprietary closed-source UIKit binaries, or Apple silicon hypervisors.
