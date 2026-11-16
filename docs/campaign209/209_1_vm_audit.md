# WinIOS Campaign 209: Independent VM & Bootstrap Audit Report

**Stage:** 209.1 — Campaign 208 Independent VM Audit  
**Campaign:** 209 — Genuine XNU VM Initialization, Physical Page Management & Kernel Memory Services  
**Date:** 2026-11-16  
**Auditor:** Principal Virtualization Architect & MMU Engineer  

---

## 1. Executive Summary

An independent technical audit was conducted on Campaign 208's virtual memory bootstrap, page table structures, physical allocator, Mach-O binary, and execution traces. The reported 586-instruction genuine XNU execution path was independently executed and reproduced with bit-exact fidelity.

### Verified Baseline Evidence:
- **Guest Instruction Retirement:** Exactly **586 retired instructions** verified in `stage208_18_genuine_xnu_expansion_tests.exe`.
- **Mach-O Binary:** `artifacts/campaign-208/xnu_genuine_init.macho` (67,848 bytes, SHA-256: `3498988245f371b8e35082d3b76ac3c111b21c5755a561aed3cd53c63b006bd4`).
- **Call Progression:** `_start` (`0x1DA0`) -> `arm_init` (`0x1648`) -> `pe_arm_init` (`0x1D84`) -> `PE_init_platform` (`0x1BAC`) -> `arm_vm_init_early` -> `pmap_bootstrap` (`0x14A0`) -> WFI halt.
- **Physical Page Allocator:** `winios::memory::XnuPhysicalPageAllocator` verified managing range `0x40000` to `0x800000` (496 frames of 16KB).

---

## 2. Page Table Structure Audit

In Campaign 208, `arm_vm_init_early` in `src/kernel/xnu_arm_init.c` sets up initial translation entries at `pt_base` (`0x30000` or `topOfKernelData` aligned to 16KB):
```c
volatile uint64_t *l1_table = (volatile uint64_t *)pt_base;
l1_table[0] = 0x0000000000000701ULL; // 32MB identity map block for DRAM base [0x0, 32MB)
l1_table[1] = 0x0000000002000701ULL; // 32MB identity map block for DRAM [32MB, 64MB)
l1_table[285] = 0x0000000220000705ULL; // AIC MMIO aperture at 0x23B100000
```

### Critical Architectural Observations for Campaign 209:
1. **Descriptor Level Semantics:** In ARMv8 16KB translation granule, a 32MB block descriptor is valid at **Level 2** ($2^{25}$ bytes), where bits [35:25] index the 2048 entries of a 16KB table covering a 64GB address space.
2. **AIC2 Descriptor Output Address Error:**
   - Index 285 corresponds to virtual range `[0x23A000000, 0x23C000000)`.
   - The descriptor `0x0000000220000705ULL` has bits [47:25] set to `0x220000000` instead of `0x23A000000`.
   - As a result, virtual address `0x23B100000` translates to physical address `0x221100000` rather than `0x23B100000`. This will be formally addressed and corrected in Stage 209.2.
3. **RAM Range Over-Allocation:**
   - Configured DRAM capacity is 8 MiB (`0x800000`).
   - Entry 0 covers 32 MiB, and Entry 1 covers 32 MiB to 64 MiB.
   - Any access beyond 8 MiB exposes nonexistent physical RAM. This will be reconciled in Stage 209.3.
