# WinIOS Campaign 205 — Stage 205.12 Report
## Kernel Boot Arguments (AppleBootArgsRev2) Builder & Memory Protocol

**Stage:** 205.12  
**Status:** COMPLETE / PASS  
**Date:** 2026-11-16  
**Host:** Windows 11 x64  
**Compiler:** Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)

---

### 1. Architectural Summary

Darwin/XNU on ARM64 expects the bootloader (iBoot / synthetic firmware) to provide a pointer in register `X0` and `X19` to the physical address of the `boot_args` structure. In LP64 Darwin kernels, this is `struct boot_args` (Revision 2, Version 2, 736 bytes):
- `Revision` (offset 0, 2 bytes): 2
- `Version` (offset 2, 2 bytes): 2
- `virtBase` (offset 8, 8 bytes): Virtual base of kernel memory mapping (typically `0xFFFFFE0007004000` on 16KB-page Darwin ARM64).
- `physBase` (offset 16, 8 bytes): Physical DRAM base address (`0x800000000` on Apple A16).
- `memSize` (offset 24, 8 bytes): Physical DRAM size (e.g., 6 GB = `0x180000000`).
- `topOfKernelData` (offset 32, 8 bytes): Physical boundary of loaded kernel image and bootstrap allocations.
- `Video` (offset 40, 48 bytes): `Boot_Video` structure (`v_baseAddr`, `v_display`, `v_rowBytes`, `v_width`, `v_height`, `v_depth`).
- `machineType` (offset 88, 4 bytes): Target device identifier.
- `deviceTreeP` (offset 96, 8 bytes): Physical pointer to flattened DeviceTree.
- `deviceTreeLength` (offset 104, 4 bytes): Length of DeviceTree blob.
- `CommandLine` (offset 108, 608 bytes): Boot command-line string (`-v debug=0x144 keepsyms=1`).
- `bootFlags` (offset 720, 8 bytes): Bitflags passed by bootloader.
- `memSizeActual` (offset 728, 8 bytes): Actual installed physical memory size.

### 2. Implementation Deliverables

- **`src/firmware/kernel_boot_args_builder.h` & `.cpp`:**
  - Implements `KernelBootArgsBuilder` providing a fluent, type-safe API for constructing and validating `AppleBootArgsRev2`.
  - Serializes to a strictly aligned 736-byte binary payload.
  - Validates structural contracts: bounds checking of `topOfKernelData` within physical memory, device-tree consistency, revision and version compatibility.
  - Implements transactional writing into guest RAM (`write_to_memory`) enforcing 8-byte alignment and bounds checking.
- **`tests/unit/stage205_12_kernel_boot_args_tests.cpp`:**
  - Tests default initialization, size checks, exact byte offset validation for all fields, contract validation failure paths, and physical memory write/readback.

### 3. Verification & Test Results

- All 4 test groups passed (0 failures).
- Zero warnings under MSVC `/W4 /WX /permissive-`.
