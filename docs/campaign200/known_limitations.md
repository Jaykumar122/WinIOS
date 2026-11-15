# WinIOS Campaign 200: Known Limitations and Scope Disclosures

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Scope Classification:** ARM64e Architectural Virtualization & Research Simulation  
**Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)  
**Milestone:** Campaign 200 Closeout  

---

## 1. Explicit Non-Goals & Architectural Boundaries

WinIOS is an architectural CPU emulator and virtualization research environment designed to execute and analyze ARM64 / ARM64e guest code, Darwin Mach-O binaries, and synthetic operating system kernels on Windows 11 x64. 

The following items are **explicitly outside the design scope** and must not be claimed:

1. **No Genuine Apple iOS Kernel Booting:**
   - WinIOS does not boot genuine Apple proprietary iOS kernels (`kernelcache`), nor does it run unmodified Apple Secure Boot chain binaries (`iboot`, `LLB`).
   - The kernel execution model tested in Campaign 200 is a synthetic ARM64/ARM64e research kernel conforming to ARM architectural specifications and Darwin userspace conventions.

2. **No Secure Enclave Processor (SEP) Emulation:**
   - The Apple Secure Enclave Processor (SEP), its proprietary microkernel (SEPOS), and its encrypted mailbox registers are not emulated. Hardware-backed Apple keychain operations requiring SEP hardware root-of-trust are not supported.

3. **Proprietary Apple Custom Hardware Blocks:**
   - Complex proprietary peripherals present in the A16 Bionic SoC—such as the Apple Neural Engine (ANE), Apple AGX GPU (Metal graphics), Apple Camera Image Signal Processor (ISP), Apple Display Controller (IOMobileFramebuffer), and Apple Secure Memory Protection Unit (AMCC)—are either stubbed, mocked at interface boundaries, or left unmodeled.
   - Peripherals modeled in WinIOS focus strictly on compute, memory, and baseline platform control: GICv3 interrupt controller, Virtual IOMMU, DMA engine, System Counter / Timers, UART / Serial Console, and Flattened Device Tree.

4. **Bounded PAC Cryptographic Primitives:**
   - The Pointer Authentication (PAC) subsystem emulates standard ARMv8.3-A / ARM64e PAC instructions (`PACIA`, `AUTIA`, `PACGA`, `BRAA`, etc.) using a deterministic QARMA/QARMA64 algorithm for research and validation purposes. It does not mimic Apple-proprietary silicon implementation quirks or secret silicon keys.

5. **Weak Memory Model Simulation vs Host Memory Model:**
   - Host execution takes place on x86-64 hardware, which enforces Total Store Order (TSO). WinIOS models ARM64 weak-memory ordering, memory barriers (`DMB`, `DSB`, `ISB`), and exclusive monitor clearing deterministically in synthetic multi-core clusters (`SMPCluster`), but does not modify the underlying host CPU hardware cache hierarchy.

6. **Host Operating System Dependency:**
   - WinIOS is engineered natively for Windows 11 x64 using Visual Studio 2026 MSVC C++20. Cross-compilation of guest test binaries requires Zig 0.17.0 targeting `aarch64-macos` / `aarch64-ios`. Execution on Linux, macOS, WSL, or Android hosts is not supported or tested in this configuration.

---

## 2. Identified Platform Limitations & Future Work

| Subsystem | Current State in Campaign 200 | Future Enhancement Opportunity |
| :--- | :--- | :--- |
| **Stage-2 MMU** | Full 16KB/4KB IPA-to-PA translation and permission checking | Nested virtualization (`EL2` guest hypervisor running `EL1` guest) |
| **Virtual IOMMU** | 16-bit Device ID table, multi-domain IOVA translation, fault reporting | Two-stage IOMMU translation (`IOVA \to IPA \to PA`) for nested guests |
| **DMA Engine** | Descriptor ring execution, buffer chaining, GICv3 SPI 48 completion | Scatter-gather list scatter write optimization with zero-copy host buffers |
| **GICv3 Interrupts** | 8 cores, SGIs, PPIs, SPIs, PMR priority masking, affinity routing | Extended LPI / ITS (Interrupt Translation Service) for virtual MSI-X |
| **Guest Binaries** | Genuine Mach-O ARM64e execution via Zig toolchain | Complex multi-threaded dynamic dyld runtime loading with chained fixups |

---

## 3. Conformance Summary

The WinIOS virtualization platform satisfies all architectural contracts established for Campaign 200 with zero compiler warnings under `/W4 /WX /permissive-` and 100% test pass rates across all 21 test suites.
