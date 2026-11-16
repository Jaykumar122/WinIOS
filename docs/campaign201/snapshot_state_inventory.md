# WinIOS Campaign 201 — Stage 201.17 Snapshot State Inventory

**Campaign:** 201  
**Stage:** 201.17  
**Repository:** `D:\WinISO26\WinIOS\`  
**Target:** Authoritative architectural inventory of platform state serialization in `.wsnap` v2 format  
**Date:** November 16, 2026  

---

## 1. Executive Summary

A comprehensive source inspection of `src/snapshot/smp_snapshot_manager.cpp`, `src/snapshot/smp_snapshot_types.h`, `src/cpu/cpu.h`, `src/device/virtual_iommu.h`, and `src/device/gicv3.h` was conducted to establish an authoritative catalog of serializable virtual platform state.

The WinIOS `.wsnap` v2 format employs a chunk-based, CRC32C-protected container architecture supporting discrete section descriptors.

---

## 2. Authoritative State Serialization Inventory

| Subsystem Component | State Element | Serialization Status | Reconstruction Status | Notes & Invariant Rules |
| :--- | :--- | :--- | :--- | :--- |
| **CPU Core Registers** | General Purpose X0–X30 | **Serialized** | **Reconstructed** | Saved per active core in `CoreArchitecturalState` |
| **CPU Core Pointers** | PC, SP_EL0, SP_EL1, SP_EL2 | **Serialized** | **Reconstructed** | Program counter and stack pointers preserved |
| **CPU PSTATE / Flags** | NZCV, DAIF, CurrentEL, SPSel | **Serialized** | **Reconstructed** | Architectural exception state fully preserved |
| **System Registers EL1** | SCTLR, TCR, TTBR0, TTBR1, MAIR, VBAR | **Serialized** | **Reconstructed** | Full MMU control configuration restored |
| **System Registers EL2** | HCR_EL2, VTTBR_EL2, VTCR_EL2 | **Serialized** | **Reconstructed** | Stage-2 hypervisor control configuration restored |
| **PAC Keyring** | APIA, APIB, APDA, APDB, APGA (128-bit) | **Intentionally Excluded** | **Reconstructed (Clean Reset)** | Hardware root secrets; reset to clean default across checkpoints to prevent forgery |
| **PAC Fault Mode** | TrapException vs CorruptUpperBits | **Intentionally Excluded** | **Reconstructed (Default)** | Architectural policy default applied on CPU initialization |
| **Physical RAM** | 48-bit Physical Address Space | **Serialized** | **Reconstructed** | Chunked sparse storage; zero chunks omitted |
| **Exclusive Monitor** | Active reservations per core | **Serialized** | **Reconstructed** | Core ID, target PA, and valid flags persisted |
| **Scheduler State** | Quantum, core states, retirement seq | **Serialized** | **Reconstructed** | Monotonic retirement sequence preserved |
| **GICv3 Distributor** | GICD_CTLR, IGROUPR, ISENABLER, IPRIORITYR | **Serialized** | **Reconstructed** | Full distributor state machine persisted |
| **GICv3 CPU Interface**| ICC_PMR_EL1, ICC_IGRPEN0, ICC_IGRPEN1 | **Serialized** | **Reconstructed** | Per-core priority masks and group enables restored |
| **Virtual IOMMU** | Domain table, device attachments, mappings | **Serialized** | **Reconstructed** | Serialized via `VirtualDeviceBus` device state blocks |
| **Virtual DMA Engine** | Descriptor ring pointer, pending transfers | **Serialized** | **Reconstructed** | Persisted as attached bus device state |
| **TLB Software Cache** | Translation cache entries | **Intentionally Excluded** | **Reconstructed (Empty)** | Architectural: TLB is invalid on cold restore; flushed |
| **Hardware Performance**| Cycle counters, PMU event counters | **Unsupported** | **Unsupported** | Synthetic time advances via instruction retirement |
| **Secure Enclave (SEP)**| Encrypted SEP keys & hardware root | **Unsupported** | **Unsupported** | Apple SEP hardware is outside research emulation scope |

---

## 3. Container Integrity Architecture

Every `.wsnap` v2 snapshot file contains:
1. **Header Block:** Magic `0x50414E5357` ("WSNAP2"), format version 2, core count, RAM size, canonical cluster hash.
2. **Directory Table:** Offsets, uncompressed sizes, compressed sizes, and CRC32C checksums for each section.
3. **Payload Sections:** Structured payloads for Metadata, CoreDirectory, CoreArchitecturalState (per core), SharedMemory, Scheduler, ExclusiveMonitor, InterruptRouter, DeviceState, and Integrity.
4. **Integrity Validation:** Upon restore, all section CRC32C digests are validated before applying any state changes to live structures, enforcing strict restore atomicity.
