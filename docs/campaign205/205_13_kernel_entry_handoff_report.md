# WinIOS Campaign 205 — Stage 205.13 Report
## Kernel Entry Handoff (`KernelBootCoordinator`)

**Stage:** 205.13  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

Stage 205.13 implements the central boot handoff engine: `KernelBootCoordinator`.
This orchestrates the end-to-end boot sequence bridging firmware/bootloader responsibilities into genuine operating system execution:
1. **Kernel Image Ingestion & Parsing:**
   - Validates Mach-O 64-bit headers, ARM64 CPU types, and segment structure (`LC_SEGMENT_64`, `LC_UNIXTHREAD`).
   - Extracts entry virtual address (`entry_pc`).
2. **Segment Mapping & Zero-Fill:**
   - Maps segments transactionally into guest physical RAM.
   - Enforces BSS zero-filling for `vmsize > filesize`.
3. **Hardware DeviceTree Construction:**
   - Builds dynamic Apple DeviceTree or FDT matching iPhone 15 A16 specifications (2 Everest P-cores + 4 Sawtooth E-cores, AIC2, PL011 UART).
   - Writes the flattened tree directly into guest RAM above the kernel segments at a 16KB boundary.
4. **Boot Arguments Handoff (`AppleBootArgsRev2`):**
   - Populates the 736-byte canonical Darwin `boot_args` structure (`virtBase`, `physBase`, `memSize`, `topOfKernelData`, `deviceTreeP`, `deviceTreeLength`, `CommandLine`, `bootFlags`).
   - Writes `boot_args` into guest RAM above the DeviceTree.
5. **CPU Privilege and Register Handoff:**
   - Transitions the ARM64 virtual CPU into EL1h mode (`current_el = 1`, `SPSel = 1`).
   - Sets active stack pointer (`SP_EL1`) above `boot_args`.
   - Programs `X0` and `X19` with the physical address of `boot_args`.
   - Sets `PC` to the kernel entry point.
   - Programs system registers (`VBAR_EL1`, `SCTLR_EL1`, `TCR_EL1`, `TTBR0/1_EL1`, `MAIR_EL1`, `SPSR_EL1`).

### 2. Implementation Deliverables

- **`src/runtime/kernel_boot_coordinator.h` & `.cpp`:**
  - Implements `KernelBootCoordinator::prepare_and_boot` coordinating image parsing, mapping, memory layout, DeviceTree and BootArgs synthesis, and CPU architectural state initialization.
- **`tests/unit/stage205_13_kernel_entry_handoff_tests.cpp`:**
  - End-to-end unit test with synthetic Mach-O binary testing successful handoff, register values, error recovery for corrupted images, and out-of-memory protections.

### 3. Verification & Test Results

- All 3 test groups PASSED (100% pass rate).
- Zero warnings under MSVC `/W4 /WX /permissive-`.
