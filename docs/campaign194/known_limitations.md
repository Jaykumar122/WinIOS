# Campaign 194 — Known Limitations & Architectural Boundaries

## 1. Scope Boundaries

This document formalizes the known architectural limitations, boundaries, and explicitly omitted features of the WinIOS synthetic kernel runtime at the conclusion of Campaign 194.

---

## 2. Enumerated Limitations

### 2.1 Absence of Authentic Apple XNU Kernel Boot
- **Limitation:** WinIOS does not decrypt, sign, or boot authentic Apple iOS `kernelcache` binaries from stock IPSW releases.
- **Architectural Basis:** Genuine Apple kernel boot requires hardware-fused Apple GID keys, Secure Enclave cryptographic coprocessors, proprietary Apple boot manifests (`apticket.der`), and complex board-specific Device Tree structures that are protected by Apple hardware DRM and encryption.
- **Current Capability:** WinIOS executes independently compiled freestanding ARM64 Mach-O synthetic kernels and Darwin userspace workloads on conformant ARM64 architecture.

### 2.2 Synthetic Kernel Workloads vs Monolithic OS
- **Limitation:** The synthetic kernel runtime demonstrates complete operational algorithms (memory management, scheduling, context switching, IPC, driver MMIO, exception dispatch) through focused freestanding ARM64 workloads rather than a monolithic millions-of-lines OS.
- **Architectural Basis:** Campaign 194 emphasizes verifiable, deterministic, and isolated verification of individual kernel primitives.

### 2.3 Simulated SMP Thread Interleaving
- **Limitation:** Dual-core execution in unit tests is simulated via deterministic instruction-cycle interleaving rather than true multi-threaded host OS thread pools for guest cores.
- **Architectural Basis:** Deterministic scheduling eliminates race conditions, timing jitter, and non-reproducible test failures on host Windows environments, ensuring 100% test pass reproducibility.

### 2.4 Device Driver Hardware Emulation Set
- **Limitation:** The device-driver interface currently provides a synthetic PL011 UART console at `0x09000000` (IRQ 33). Complex PCIe, NVMe, and GPU MMIO drivers are not implemented.
- **Architectural Basis:** Console I/O provides the necessary foundational device abstraction for early kernel boot and diagnostics without introducing massive device emulation dependencies.

---

## 3. Future Roadmap Recommendations

1. **Synthetic VFS and Filesystem Driver:** Build an in-memory synthetic file system driver (e.g. ramfs/tmpfs) adhering to the `DeviceDriver` interface.
2. **Dynamic Task Forking and Exec:** Implement guest-side `fork`/`exec` semantics mapping new address spaces via `VirtualPageTableManager`.
3. **Enhanced ARM64e PAC Verification:** Wire PAC instruction verification into kernel exception frames and context switch routines.
