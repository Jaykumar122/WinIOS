# WinIOS Campaign 205 — Stage 205.5 Engineering Report
## Kernel Memory Layout Manager & W^X Enforcement

**Date:** November 16, 2026  
**Campaign:** 205  
**Stage:** 205.5  
**Deliverables:**
- `src/memory/kernel_memory_layout.h`
- `src/memory/kernel_memory_layout.cpp`
- `tests/unit/stage205_5_kernel_memory_layout_tests.cpp`

---

### 1. Implementation Overview
Stage 205.5 expands `KernelMemoryLayout` into a kernel memory manager for ARM64 Darwin/XNU boot architectures.

Key capabilities:
1. **Comprehensive Region Typing:**
   - `KernelText`: Executable kernel text (`__TEXT`), strictly `R-X`.
   - `ReadOnlyData`: Constant and relocation-protected data (`__DATA_CONST`), `R--`.
   - `WritableData`: Mutable kernel data (`__DATA`), `RW-`.
   - `ZeroInitData`: BSS and uninitialized memory (`__BSS`), `RW-`.
   - `BootInfo`: Bootloader argument structure (`boot_args`), `R--`.
   - `DeviceTree`: Flattened device tree blob, `R--`.
   - `PageTables`: Initial bootstrap translation tables, `RW-`.
   - `KernelStack`: Initial kernel stack with pre-guard and post-guard pages.
   - `KernelHeap`: Dynamic memory pool.
   - `GuardPage`: Inaccessible guard boundaries with zero permissions.
   - `MmioDevice` & `DirectPhysicalMap`: Peripheral MMIO and linear physical RAM map.
2. **Strict W^X Policy Enforcement:**
   `check_wx_policy()` and `add_region()` verify that no memory region can simultaneously possess write and execute permissions (`is_writable && is_executable`), rejecting insecure configurations.
3. **Dynamic Image-Derived Layout:**
   `build_from_parsed_image()` automatically translates a `KernelParsedImage` into a verified, collision-free memory layout, laying out segments, boot arguments, devicetree, page tables, and stacks aligned to the 16KB translation granule (`0x4000`).

---

### 2. Verification & Test Results
Test suite `stage205_5_kernel_memory_layout_tests` executed with 4 test groups:
- Test 1: Canonical kernel memory layout validation and guard page checks.
- Test 2: Dynamic layout generation from parsed Mach-O image (`__TEXT`, `__DATA_CONST`, `__DATA`, `BootArgs`, `DeviceTree`, `PageTables`, `InitialKernelStack`).
- Test 3: W^X policy violation detection and rejection.
- Test 4: Overlapping and misaligned region rejection.

**All 4 tests PASSED.** Zero warnings under `/W4 /WX /permissive-`.

**Stage 205.5 Status: PASSED**
