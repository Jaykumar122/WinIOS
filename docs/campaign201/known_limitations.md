# WinIOS Campaign 201 — Architectural Limitations & Platform Conformance Disclosures

**Campaign:** 201  
**Stage:** 201.23  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Architecture:** Windows 11 x64  
**Date:** November 16, 2026  
**Status:** Canonical Reference Document  

---

## 1. Executive Disclosure

WinIOS is an independent, clean-room ARM64/ARM64e virtualization and research execution platform designed to study and simulate Darwin userspace binaries, synthetic kernel execution environments, and architectural subsystems on Windows 11 x64.

In strict adherence to technical truth and engineering rigor, this document provides an exhaustive, unambiguous disclosure of all known architectural boundaries, approximations, and intentionally excluded subsystems within the platform as of Campaign 201 closeout.

---

## 2. Core Architecture & Hardware Identity Disclosures

### 2.1 Virtual SMP Topology vs. Physical A16 Bionic Topology
- **Physical A16 Bionic (`T8120`) Profile:** A heterogeneous 6-core processor comprising 2 high-performance "Everest" cores and 4 energy-efficient "Sawtooth" cores.
- **WinIOS Virtual Execution Model:** Provides a configurable symmetric multi-processing (SMP) cluster supporting between 1 and 8 virtual cores (`SmpScheduler`).
- **Architectural Boundary:** The 8-core configuration is a virtual research capability for stress testing SMP synchronization and lock algorithms; it must never be misattributed as physical 8-core A16 hardware.

### 2.2 Pointer Authentication (PAC) Cryptographic Model
- **Architectural Implementation:** WinIOS implements ARM64e Pointer Authentication (PAC) using a fast, deterministic 3-round SipHash PRF (Pseudo-Random Function) producing a truncated 16-bit cryptographic tag placed in bits `[63:48]` of authenticated pointers.
- **Distinction from Hardware:** Physical ARMv8.3-A hardware implements QARMA-64 (or QARMA-128/SHA-3 derivatives). WinIOS utilizes SipHash-derived PRF for host execution performance, deterministic replay fidelity, and cross-platform reproducibility without cryptographic export encumbrances.
- **Instruction Support:** Validated for `PACIA`, `PACIZA`, `PACDA`, `PACDZA`, `AUTIA`, `AUTIZA`, `AUTDA`, `AUTDZA`, `PACGA`, and `XPACD`/`XPACI`.

---

## 3. Memory Subsystem & MMU Boundaries

### 3.1 Translation Granules & Address Space
- **Granule Support:** WinIOS strictly models the 16KB translation granule standard required by modern Apple Silicon Darwin targets (4-level translation walk: L0, L1, L2, L3).
- **Interleaving Limitations:** Mixed-granule interleaving (e.g. 4KB Stage-1 alongside 16KB Stage-2) is unsupported.
- **Address Space:** Up to 48-bit Virtual Address (VA) and Intermediate Physical Address (IPA) spaces are supported.

### 3.2 Virtual IOMMU & DMA Isolation
- **IOMMU Capability:** Implements 16KB IOVA-to-PA domain-isolated translations with multi-device domain attachment and access permissions (`Read`, `Write`, `ReadWrite`).
- **Hardware Bus Limitations:** DMA operations occur directly within host memory buffers using simulated descriptor rings. Physical PCIe link training, PCIe Configuration Space enumeration, and physical MSI-X hardware interrupt delivery are outside platform scope.

---

## 4. Interrupt Controller & Platform Services

### 4.1 GICv3 Interrupt Architecture
- **Emulated Scope:** Models Distributor (`GICD`) and Redistributor (`GICR`) register decoding, Group 0 (FIQ) and Group 1 (IRQ) classification, Priority Masking (`ICC_PMR_EL1`), Binary Point Preemption (`ICC_BPR_EL1`), and SPI affinity routing (`GICD_IROUTER<n>`).
- **Approximations:** Virtual LPIs (Locality-specific Peripheral Interrupts) and ITS (Interrupt Translation Service) translation tables are not modeled.

### 4.2 Secure Enclave Processor (SEP)
- **Status:** Outside research emulation scope.
- **Implications:** Hardware-bound SEP key derivation, encrypted biometrics, and dedicated SEP mailbox transactions are unsupported.

### 4.3 Performance Monitors (PMU) & Architectural Timers
- **Timing Model:** Time advances monotonically through instruction retirement rather than true host microarchitectural cycle counts. PMU event counters track retired instructions, memory operations, and exceptions deterministically.

---

## 5. Summary Classification Matrix

| Feature / Subsystem | Architectural Classification | Implementation Method |
| :--- | :--- | :--- |
| **ARM64 User Instructions** | Fully Implemented & Differentially Tested | Native C++20 Instruction Interpreter vs. Unicorn 2.1.4 |
| **ARM64e Pointer Auth** | Implemented & Specification-Derived | 3-Round SipHash Truncated PRF (16-bit Tag) |
| **Stage-1 / Stage-2 MMU** | Fully Implemented & Differentially Tested | 16KB Granule 4-Level Page Table Walker |
| **Virtual IOMMU** | Implemented & Stress-Tested | Multi-Domain IOVA-to-PA Isolation Engine |
| **GICv3 Routing** | Implemented & Specification-Derived | Memory-Mapped GICD/GICR + ICC System Registers |
| **SMP Concurrency** | Implemented & Concurrency-Tested | Multithreaded SmpScheduler with Spinlocks & Contention Models |
| **Snapshot / Replay** | Fully Implemented & Bit-Exact Tested | CRC32C `.wsnap` v2 Container with Deterministic Stepping |
| **Darwin Userspace (dyld)** | Implemented & Binary-Tested | Mach-O 64-bit Loader, Fixups, Syscalls & LibSystem Traps |
| **Apple Kernel Boot** | Synthetic Model (No Genuine iOS Boot) | Clean-Room Synthetic Kernel Framework |
