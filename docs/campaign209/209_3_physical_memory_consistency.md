# WinIOS Campaign 209: Physical Memory & Mapping Consistency Audit

**Stage:** 209.3 — Physical-Memory and Mapping Consistency  
**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Date:** 2026-11-16  
**Auditor:** Principal Virtualization Architect & Physical Memory Specialist  

---

## 1. Executive Summary

A full architectural reconciliation of the 8 MiB physical DRAM layout, physical frame accounting, page table structures, and MMIO peripheral regions was conducted.

The audit identified two critical inconsistencies in Campaign 208:
1. **Ghost Memory Exposure via Block Descriptor 1:** Entry 1 of the Level-2 page table mapped `[32 MiB, 64 MiB)` as normal RAM (`0x0000000002000701ULL`), despite the guest platform being configured with only 8 MiB of physical DRAM.
2. **DRAM Over-Mapping via Block Descriptor 0:** Entry 0 of the Level-2 page table mapped `[0, 32 MiB)` as a monolithic block, exposing 24 MiB of nonexistent memory past the 8 MiB physical boundary.

---

## 2. Reconciled 8 MiB Physical DRAM Map

| Physical Address Range | Size | Allocation / Role | Page Count (16KB) | Security / Status |
| :--- | :--- | :--- | :--- | :--- |
| `0x00000000 - 0x00003FFF` | 16 KB | Null Guard & Reset Vector Base | 1 page | Protected / Reserved |
| `0x000014A0 - 0x00003FFF` | ~11 KB | Kernel Mach-O (`__TEXT`, `__DATA`) | Part of page 0 | Read-Only Executable / Loaded |
| `0x00004000 - 0x00007FFF` | 16 KB | Segment Padding & Mach-O GOT | 1 page | Read-Only |
| `0x00008000 - 0x0000BFFF` | 16 KB | Data Segment (`__data`) | 1 page | Read-Write |
| `0x0000C000 - 0x0000FFFF` | 16 KB | Initial Bootstrap Stack (`SP_EL1`) | 1 page | Read-Write (16KB aligned) |
| `0x00010000 - 0x0001FFFF` | 64 KB | Kernel Global State & State Machine | 4 pages | Read-Write (`g_arm_init_state`, `g_pe_state`) |
| `0x00020000 - 0x00027FFF` | 32 KB | Stolen Early Translation Tables (L1/L2 Root) | 2 pages | Read-Write / Reserved by `arm_vm_init` |
| `0x00028000 - 0x0003FFFF` | 96 KB | Kernel Metadata & Reserved Stolen Zone | 6 pages | Reserved (`topOfKernelData` expansion) |
| `0x00040000 - 0x007FFFFF` | 7.75 MiB | Free Allocatable Physical DRAM Frames | **496 pages** | Managed by `XnuPhysicalPageAllocator` |
| **Total DRAM** | **8.0 MiB** | **Physical RAM Capacity** | **512 pages** | Bounds: `[0x0, 0x800000)` |

---

## 3. Peripheral & Device MMIO Apertures (Outside DRAM)

| Physical Address Range | Size | Device Description | Mapping Type | MMIO Model |
| :--- | :--- | :--- | :--- | :--- |
| `0x00090000 - 0x00090FFF` | 4 KB | Apple S5L / S8000 Virtual UART | Normal / Device | Emulated serial output |
| `0x023B100000 - 0x023B1FFFFF`| 1 MB | Apple AIC2 Interrupt Controller | Device-nGnRE (`0x705`) | Full hardware model (6 cores) |

---

## 4. Remediation Directives for Campaign 209

1. **Eliminate Entry 1:** Remove `l1_table[1] = 0x02000701ULL`. Entry 1 must be strictly invalid (`0x0`) to prevent access to nonexistent physical memory between 32 MiB and 64 MiB.
2. **Correct Entry 285:** Update `l1_table[285]` from `0x0220000705ULL` to `0x000000023A000705ULL`, establishing a 1:1 identity mapping from VA `0x23B100000` to PA `0x23B100000`.
3. **Physical Bounds Enforcement:** In `XnuPhysicalPageAllocator`, strictly constrain the allocation envelope to `avail_start = 0x40000` and `avail_end = 0x800000`. Ensure requests outside `[0x40000, 0x800000)` are rejected with an explicit error.
