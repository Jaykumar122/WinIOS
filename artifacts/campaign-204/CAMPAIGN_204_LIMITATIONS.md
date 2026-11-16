# WinIOS Campaign 204 — Bounded Architectural Limitations

**Campaign:** 204  
**Date:** November 16, 2026  
**Scope:** Public ARM64 Mach-O Application Execution & Darwin Runtime Compatibility  

---

## 1. Intentional Architectural Boundaries

WinIOS Campaign 204 delivers a bounded, reproducible synthetic Darwin execution environment tailored specifically for public ARM64 Mach-O binaries. In keeping with high-assurance systems engineering principles, certain domains remain explicitly bounded or stubbed rather than partially or unsafely simulated.

### 1.1 Syscall Surface Boundaries
- **Unimplemented Syscalls:** Only essential Darwin BSD syscalls (~30 calls) and Mach message traps required by standard CLI binaries and public fixtures are implemented. Calls related to auditing (`audit_session_self`), kernel authorization (`kauth`), complex Mach VM traps (`vm_remap`, `mach_vm_wire`), and network sockets (`socket`, `bind`, `connect`) return `ENOSYS` or stubbed values.
- **Process Forking (`SYS_fork`):** Forking guest address spaces into separate host OS processes is not supported; `SYS_fork` returns `ENOSYS` or simulated single-process PID. Multi-threading is supported via `DarwinThreadRuntime` and `pthread_create`.
- **Signal Delivery:** Basic signal masking (`sigprocmask`) and signal action registration (`sigaction`) are tracked in process state, but asynchronous guest signal interrupts (e.g., hardware-driven SIGSEGV or SIGALRM delivery interrupting arbitrary CPU cycles) are deferred to future hypervisor campaigns.

### 1.2 Binary & Loader Boundaries
- **Encrypted / FairPlay Binaries:** WinIOS does not decrypt Apple FairPlay-protected binaries (`LC_ENCRYPTION_INFO`). Only unencrypted public Mach-O binaries and synthetic fixtures are supported.
- **Code Signing Enforcement:** Code signatures (`LC_CODE_SIGNATURE`) and Apple Developer certificates are parsed and validated structurally, but cryptographic root-of-trust anchoring to Apple's Secure Boot chain is mocked/bypassed for development binaries.
- **Objective-C / Swift Runtime Initialization:** High-level Apple frameworks (Foundation, UIKit, CoreGraphics, SwiftUI) require complex Objective-C runtime metadata registration (`__objc_imageinfo`, class tables, selectors). Campaign 204 focuses on C/POSIX runtime (`libSystem.B.dylib`). Full Objective-C / Swift runtime binding remains a target for subsequent campaigns.

### 1.3 Memory & Virtualization Boundaries
- **Granule Enforcement:** WinIOS strictly enforces a 16KB page granule (`0x4000`). Systems or binaries compiled assuming 4KB Darwin granules will be rejected by `UserspaceMachOLoader` to maintain architectural alignment with Apple Silicon iPhone 15 hardware.
- **Hardware Acceleration:** Guest ARM64 instructions execute through the WinIOS cycle-accurate synthetic CPU interpreter and userspace execution engine, not host hardware virtualization (e.g. WHPX / Hyper-V ARM64 virtualization, which is unavailable on x64 host CPUs).

---

## 2. Roadmap for Future Campaigns
1. **Socket & Network IPC:** Extend `DarwinBsdSubsystem` with virtual loopback sockets, Unix domain sockets, and packet routing.
2. **Objective-C Runtime Integration:** Implement synthetic `libobjc.A.dylib` selector dispatch and class table registry.
3. **Mach Exception Ports:** Full Mach exception handling architecture (`mach_port_t exception_port`, `exception_raise`).
