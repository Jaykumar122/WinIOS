# Stage 194.2 — Kernel Virtual Memory Layout and Mapping

## 1. Architectural Overview
Stage 194.2 establishes the canonical ARM64 virtual address space layout for the synthetic EL1 kernel in WinIOS, adhering to ARMv8.6-A architectural splits (TTBR0_EL1 for user lower-half and TTBR1_EL1 for kernel upper-half) and Apple A16 Bionic 16 KB page granules (`kPageGranule16KB = 16384`).

The memory model implements:
1. **Canonical Upper/Lower Half Separation:**
   - User space (EL0): `0x0000000000000000` to `0x00007FFFFFFFFFFF` (mapped via `TTBR0_EL1`).
   - Kernel space (EL1): `0xFFFF800000000000` to `0xFFFFFFFFFFFFFFFF` (mapped via `TTBR1_EL1`).
2. **Explicit Memory Regions and Protection Attributes:**
   - `KernelText` (`0xFFFF800001000000`): Read + Execute (RX), strictly non-writable ($W \oplus X$).
   - `KernelData` (`0xFFFF800002000000`): Read + Write (RW), strictly non-executable ($W \oplus X$).
   - `KernelHeap` (`0xFFFF800004000000`, 256 MB): Dynamic allocator window, Read + Write (RW).
   - `KernelStack` (`0xFFFF800020000000`, 256 KB): Per-CPU / per-thread stacks, Read + Write (RW).
   - `MmioDevices` (`0xFFFF800030000000`, 256 MB): Virtual device register window.
   - `DirectPhysicalMap` (`0xFFFF800040000000`, up to 2 GB): Linear physical DRAM mapping.
3. **Zero-Permission Guard Page Isolation:**
   - 16 KB zero-permission guard pages (`kGuardPageSize = 16384`) placed immediately preceding and following the kernel heap and stack pools to catch overflows and underflows deterministically.

---

## 2. Implementation Summary

### 2.1 File Changes
- **Header:** `src/memory/kernel_memory_layout.h`
- **Source:** `src/memory/kernel_memory_layout.cpp`
- **Build Target:** Registered `kernel_memory_layout.cpp` in `winios_core` and added `stage194_2_kernel_memory_layout_tests` in `CMakeLists.txt`.

### 2.2 Canonical Memory Map Table

| Region Name | Virtual Address Range | Size | Physical Base | Protection | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `KernelText` | `0xFFFF800001000000` - `0xFFFF800002000000` | 16 MB | `DRAM + 0x01000000` | `R-X` | Executable kernel code |
| `KernelData` | `0xFFFF800002000000` - `0xFFFF800003000000` | 16 MB | `DRAM + 0x02000000` | `RW-` | Read-write globals & descriptors |
| `HeapPreGuard` | `0xFFFF800003FFC000` - `0xFFFF800004000000` | 16 KB | N/A | `---` | Underflow trap |
| `KernelHeap` | `0xFFFF800004000000` - `0xFFFF800014000000` | 256 MB | `DRAM + 0x04000000` | `RW-` | Dynamic kernel heap |
| `HeapPostGuard`| `0xFFFF800014000000` - `0xFFFF800014004000` | 16 KB | N/A | `---` | Overflow trap |
| `StackPreGuard`| `0xFFFF80001FFFC000` - `0xFFFF800020000000` | 16 KB | N/A | `---` | Stack underflow trap |
| `KernelStack`| `0xFFFF800020000000` - `0xFFFF800020040000` | 256 KB | `DRAM + 0x08000000` | `RW-` | Kernel execution stacks |
| `StackPostGuard`|`0xFFFF800020040000` - `0xFFFF800020044000` | 16 KB | N/A | `---` | Stack overflow trap |
| `MmioDevices` | `0xFFFF800030000000` - `0xFFFF800040000000` | 256 MB | N/A (MMIO) | `RW-` | Virtual peripherals & UART |
| `DirectPhysicalMap`| `0xFFFF800040000000` - `...` | Up to 2 GB | `DRAM + 0x0` | `RW-` | Direct linear physical map |

---

## 3. Verification Matrix

The test suite `tests/unit/stage194_2_kernel_memory_layout_tests.cpp` verifies all invariants:

| Test Case | Description | Result |
| :--- | :--- | :--- |
| `test_canonical_virtual_address_partitioning` | Tests TTBR0 user vs TTBR1 kernel boundary predicates. | **PASS** |
| `test_canonical_regions_and_permissions` | Confirms strict W^X enforcement and exact sizes. | **PASS** |
| `test_guard_page_isolation` | Confirms guard pages surround heap and stack with zero access permissions. | **PASS** |
| `test_direct_mapping_translations` | Tests bi-directional physical <-> virtual address calculations. | **PASS** |
| `test_region_lookup_and_layout_validation` | Validates 16KB alignment and zero overlap across all regions. | **PASS** |

Tested in both **Debug** and **Release** builds under MSVC C++20 `/W4 /WX /permissive-`.
