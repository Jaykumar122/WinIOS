# Campaign 193 — Known Limitations & Architectural Boundaries

## 1. Scope Boundaries

This document enumerates the known architectural limitations, boundaries, and explicitly omitted features of the WinIOS emulation engine at the conclusion of Campaign 193.

---

## 2. Enumerated Limitations

### 2.1 Absence of Authentic Apple XNU Kernel Boot
- **Limitation:** WinIOS does not boot genuine proprietary Apple iOS/macOS kernel caches (`kernelcache`) from raw Apple restore IPSW images.
- **Root Cause:** Proprietary Apple silicon boot relies on Secure Boot ROM, cryptographic Apple Root Certificates, encrypted IMG4 payloads, and proprietary hardware coprocessors (Secure Enclave Processor / SEP) that cannot be legally or technologically replicated without authentic private hardware keys.
- **Remediation / Current Capability:** WinIOS executes independently compiled freestanding ARM64 guest kernels (`synthetic_el1_kernel`) and authentic compiled userspace Mach-O applications on an architectural Darwin userspace runtime.

### 2.2 Lack of AArch32 Execution Support
- **Limitation:** `CPU::supports_aarch32_lower_el()` returns `false`. Attempts to enter Lower EL AArch32 vectors are unallocated.
- **Architectural Basis:** Conforms to modern 64-bit-only ARMv9 and Apple Silicon cores (A16 Bionic `T8120`, Apple M2/M3/M4), which omit 32-bit hardware execution state.

### 2.3 Hypervisor (EL2) and Secure Monitor (EL3) Virtualization in Test Mode
- **Limitation:** In userspace and synthetic kernel test environments, EL2/EL3 trapping is disabled by default unless explicitly configured by the test harness.
- **Architectural Basis:** Single-OS kernel testing runs at EL1 without hypervisor intervention. Full EL2 stage-2 translation and PSCI EL3 handling are implemented in dedicated unit test suites (`el3_tests`, `psci_tests`) rather than active during general userspace execution.

### 2.4 Monotonic Clock Progression
- **Limitation:** Virtual counter progression occurs deterministically through simulated cycles or explicit `advance_physical_counter` invocations rather than asynchronous host timer signals.
- **Architectural Basis:** Guarantees absolute repeatability and determinism across Windows host execution environments without race conditions.

---

## 3. Future Roadmap Recommendations
1. **Dynamic Page Table Manipulation in Guest Kernel:** Expand `synthetic_el1_kernel` to allocate and construct hierarchical Stage-1 page tables entirely in ARM64 assembly.
2. **Preemptive Round-Robin Scheduling:** Implement multi-task preemptive context switching driven by periodic virtual timer interrupts across multiple guest threads.
3. **Enhanced ARM64e PAC Support:** Complete cryptographic verification gates for pointer authentication instructions (`PACIA`, `AUTIA`) in kernel exception handlers.
