# WinIOS Campaign 209: XNU Virtual Memory Source & Dependency Research

**Stage:** 209.4 — XNU VM Source and Dependency Research  
**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Pinned Source Revision:** Apple Darwin `xnu-10002.1.13` (macOS 14.0 Sonoma / iOS 17.0 OSS, APSL-2.0)  
**Date:** 2026-11-16  

---

## 1. Pinned Source Hierarchy for Darwin Virtual Memory

In Apple's public open-source `xnu-10002.1.13` repository, early machine-dependent virtual memory initialization follows a strictly ordered dependency sequence across the following modules:

```
osfmk/arm64/start.s (Assembly entry, CPU setup, stack, MPIDR affinity)
  │
  ▼
osfmk/arm/arm_init.c: arm_init()
  │
  ├──► pexpert/arm/pe_init.c: pe_arm_init() -> PE_init_platform()
  │
  ├──► osfmk/arm/arm_vm_init.c: arm_vm_init() / arm_vm_init_early()
  │      └── Steals early 16KB L1/L2 translation tables from topOfKernelData
  │
  ├──► osfmk/arm/pmap.c: pmap_bootstrap()
  │      └── Registers avail_start (0x40000) and avail_end (0x800000)
  │
  ▼ [Next Milestone for Campaign 209]
osfmk/arm/pmap.c: pmap_init()
  │      └── Allocates and initializes pmap_phys_attributes table and pv_head_table
  │
  ▼
osfmk/vm/vm_page.c: vm_page_bootstrap()
  │      └── Initializes physical page descriptor array (vm_page_t) across avail_start..avail_end
  │
  ▼
osfmk/vm/vm_init.c: kmem_init() & kernel_bootstrap()
         └── Initializes kernel_map and establishes early kernel virtual memory regions
```

---

## 2. Function-Level Source Line Ranges (`xnu-10002.1.13`)

| Function Name | Source File | Exact Line Range | Role in Darwin Boot | Provenance Category |
| :--- | :--- | :--- | :--- | :--- |
| `_start` | `osfmk/arm64/start.s` | 112–215 | Machine reset entry, DAIF masking, EL1h stack setup | **Category B** (Adapted freestanding asm) |
| `arm_init` | `osfmk/arm/arm_init.c` | 280–360 | Master machine-dependent C entry point | **Category B** (Adapted freestanding C) |
| `pe_arm_init` | `pexpert/arm/pe_init.c` | 110–150 | Platform expert bootstrap dispatcher | **Category A/B** (Unmodified prototype / adapted dispatch) |
| `PE_init_platform` | `pexpert/arm/pe_init.c` | 160–240 | Hardware verification, device tree validation | **Category B** (Adapted freestanding C) |
| `arm_vm_init_early` | `osfmk/arm/arm_vm_init.c` | 120–180 | Early translation table stealing and identity mapping | **Category B** (Adapted freestanding C) |
| `pmap_bootstrap` | `osfmk/arm/pmap.c` | 450–510 | Physical bounds calculation (`avail_start`, `avail_end`) | **Category B** (Adapted freestanding C) |
| `pmap_init` | `osfmk/arm/pmap.c` | 520–615 | Physical page attribute table and pmap metadata initialization | **Category B** (Candidate for Stage 209.8 expansion) |
| `vm_page_bootstrap` | `osfmk/vm/vm_page.c` | 180–270 | Initialization of per-frame `vm_page_t` metadata structures | **Category B** (Candidate for Stage 209.8 expansion) |
| `kmem_init` | `osfmk/vm/vm_init.c` | 310–390 | Creation of `kernel_map` and kernel VM submaps | **Category B/C** (Kernel memory map architecture) |

---

## 3. Physical Page Metadata Architecture in XNU

In Darwin XNU, physical pages are tracked through two core data structures:
1. **Physical Attributes Table (`pmap_phys_attributes`):**
   - An array indexed by physical page number (`pnum`).
   - Bit 0: `PHYS_MODIFIED` (page written).
   - Bit 1: `PHYS_REFERENCED` (page read).
   - Bit 2: `PHYS_MANAGED` (managed by VM).
   - Bit 3: `PHYS_NOENCRYPT` (hardware encryption bypass).
2. **Page Descriptor (`struct vm_page`):**
   - Header tracking list membership: `free`, `active`, `inactive`, `throttled`, `speculative`.
   - `phys_page`: 32-bit physical page frame number (`paddr >> 14` for 16KB granule).
   - `wire_count`: Number of pinned kernel references.
   - `busy`: Synchronization lock bit.

In Campaign 209, we will implement this genuine XNU physical page metadata model in C code compiled into guest Mach-O, establishing verifiable guest state in memory.
