# WinIOS Campaign 221 — Capability Matrix & Architectural Boundaries

## Purpose & Scope

This document defines the formal capability classification matrix of the WinIOS research platform as of Campaign 221 closeout. It explicitly establishes what capabilities are implemented, what components are freestanding research models, and what boundaries exist relative to production iOS, Darwin, XNU, and Apple platforms.

---

## 1. Five-Level Architectural Capability Classification

| Level | Capability Category | Implementation Status | Implementation Details & Evidence |
|:---:|:---|:---:|:---|
| **Level 1** | **ARM64 Architectural Execution** | **GENUINE (Architectural)** | EL0/EL1 execution modes, 16 KB translation granule MMU, stage-1 user page tables with ASID tagging (TCR_EL1, TTBR0_EL1, MAIR_EL1), CPACR_EL1 FP/SIMD access control, complete 35-field architectural context register preservation (X0-X30, SP_EL0, PC, PSTATE, ESR_EL1), unforgeable `ExecutionBoundaryEnforcer` retirement tracking. |
| **Level 2** | **Freestanding C Runtime & SDK v1.5** | **GENUINE (Freestanding C)** | Custom `crt0.s` AArch64 entry adapter, freestanding C runtime (`winios_runtime.c`) with zero host libc dependencies, AAPCS64 compliant calling convention, 16-byte stack alignment, structured argument block parsing (`winios_app_arg_block_t`), environment variable lookup, string formatting and integer conversions. |
| **Level 3** | **Darwin Syscall & Extension Interface** | **CONFORMANT (Category A/B/C)** | Strict Darwin XNU Category A/B syscall emulation (SYS_exit, SYS_read, SYS_write, SYS_open, SYS_close, SYS_getpid, SYS_pipe, SYS_sched_yield), Category C WinIOS extensions (Syscalls 500–532) for windowing, surface rendering, event polling, private storage sandboxes, guest logging, and process lifecycle management. |
| **Level 4** | **Packaging, Storage & Security Sandboxing** | **ROBUST (Research Sandbox)** | `WIOSPKG2` binary package specification, deterministic 5-run bit-for-bit packaging, SHA-256 integrity manifest, path traversal guards (`..` rejection), isolated per-application storage directories (`/data/<bundle_id>`), atomic transactional writes, and discrete bitmask capability management. |
| **Level 5** | **Native ARM64 Applications & Ecosystem** | **FUNCTIONAL (Freestanding Apps)** | Three native ARM64 guest applications (`Runtime Explorer`, `Notes`, `Process Monitor`) cross-compiled with Zig 0.17.0 into real Mach-O images, verified executing in EL0, displaying graphics, persisting files, and terminating with distinctive exit codes (42, 43, 44). |

---

## 2. Explicit Non-Claims & Research Delineations

To ensure strict engineering integrity, WinIOS explicitly records the following boundaries:

- **Not Production iOS:** WinIOS is not Apple iOS, iPadOS, macOS, or watchOS. It is a Windows 11-hosted research emulation and ABI translation environment.
- **Not Production UIKit or SwiftUI:** The WinIOS guest UI framework provides lightweight rasterization, dirty-rectangle tracking, and primitive surface manipulation. It does not implement Apple UIKit, SwiftUI, CoreAnimation, or AppKit.
- **Not Metal:** Surface rendering is executed via software framebuffers or Windows DirectX 11 / Host GPU blitting. Apple Metal API is not implemented.
- **Not Complete Apple XNU:** The Mach-message subsystem and IPC router provide a bounded research approximation of Darwin ports and task rights; they do not implement full XNU mach_msg semantics or MIG stubs.
- **Cross-Compilation:** Guest applications are cross-compiled using the open-source Zig compiler targeting freestanding `aarch64-macos`. No proprietary Apple SDKs or tools are required.
