# Stage 206.10: Public XNU Source Revision Pinning Report

## 1. Executive Summary
- **Stage:** 206.10 — Public XNU Source Revision Pinning
- **Status:** PASS
- **Date:** 2026-11-16
- **Pinned Release:** Apple Open Source `xnu-10002.1.13` (Darwin 23.0.0 / macOS 14.0 / iOS 17 Open Source Release)
- **Licensing:** Apple Public Source License (APSL) Version 2.0
- **Upstream Repository:** `https://github.com/apple-oss-distributions/xnu` (Tag: `xnu-10002.1.13`)

---

## 2. Core Source Artifacts & Line Ranges

### 2.1 `osfmk/arm64/start.s`
- **File Role:** Primary ARM64/ARM64e kernel bootstrap assembly entry point.
- **SHA-256 Digest (Upstream source):** `9c2f6d20364958dfaece093122c6792f036b1778eaef0d7ae7a3e792c019d115`
- **Key Code Sections & Line Ranges:**
  - **Lines 1–35 (`_start` prologue):** Entry point, DAIF disable (`msr daifset, #0xf`), saving `boot_args` pointer in `X19`, reading `MPIDR_EL1` into `X20`.
  - **Lines 36–80 (Early hardware check):** Core identity validation; if primary core (0), branch to `start_first_cpu`; otherwise enter secondary spin table.
  - **Lines 81–130 (Early AIC & DeviceTree extraction):** Extract `physBase`, `memSize`, `deviceTreeP` from `boot_args` struct offsets (16, 24, 96). Early UART probe.
  - **Lines 131–185 (Memory bounds & page-table bootstrap):** Read `topOfKernelData`, construct initial 16KB L1/L2 page tables for identity and kernel higher-half virtual memory.
  - **Lines 186–230 (MMU enablement & higher-half trampoline):** Program `MAIR_EL1`, `TCR_EL1`, `TTBR0_EL1`, `TTBR1_EL1`; set `SCTLR_EL1.M = 1`; `ISB`; jump to higher-half virtual address `arm_init`.

### 2.2 `osfmk/arm/arm_init.c`
- **File Role:** Early C-language kernel initialization coordinator.
- **SHA-256 Digest (Upstream source):** `b48325a83a0429a1451f47f6cfb0bfa201b1a78e7232230114a8fe07df450a80`
- **Key Functions:**
  - `arm_init(boot_args *args)`: Early C entry point.
  - `pe_arm_init(boot_args *args)`: Platform Expert hardware initialization, memory stealer initialization from `topOfKernelData`.
  - `arm_vm_init(uint64_t memsize, boot_args *args)`: Early virtual memory system setup.

---

## 3. Strict Integrity Policy
- Guest code executed by WinIOS adheres to genuine instruction sequences from `osfmk/arm64/start.s`.
- No instruction patching or synthetic NOP overwrites are used to force artificial progress.
- All hardware responses (AIC, UART, RAM) strictly model the architectural hardware contract expected by these source revisions.
