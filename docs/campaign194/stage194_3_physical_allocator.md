# Stage 194.3 — Guest Physical Memory Manager (16KB Page Allocator)

## 1. Architectural Scope & Evidence Classification
Stage 194.3 implements the physical page management subsystem for the synthetic ARM64 kernel runtime in WinIOS:
- **Evidence Classification:** `GUEST_MEMORY_MANAGER`
- **Page Granule:** 16 KB (`kPhysicalPageSize16KB = 16384`), adhering to the Apple A16 Bionic architecture.
- **Physical Allocation Representation:** Packed bitfield bitmap resident directly in guest RAM.
  - Bit value `0`: Physical page frame is free.
  - Bit value `1`: Physical page frame is allocated or reserved.
- **Dual-Mode Allocation Capability:**
  1. **Host-Side Management Engine:** `PhysicalPageManager` in `src/memory/physical_page_manager.h` and `src/memory/physical_page_manager.cpp` provides initialization, reservation of firmware/kernel ranges, contiguous multi-page allocation, deallocation, and statistical auditing directly over guest memory.
  2. **Guest-Side EL1 Assembly Routines:** `palloc_16k` and `pfree_16k` execute directly in the simulated ARM64 guest CPU at EL1, manipulating the guest RAM bitmap with standard ARM64 instructions (`LDRB`, `STRB`, `LSL`, `ORR`, `BIC`).

---

## 2. Artifact and Implementation Summary

### 2.1 File Changes
- **Header:** `src/memory/physical_page_manager.h`
- **Source:** `src/memory/physical_page_manager.cpp`
- **Guest Assembly Source:** `tests/artifacts/src/guest_physical_allocator.S`
- **Compiled Mach-O Binary:** `tests/artifacts/bin/guest_physical_allocator`
  - Format: 64-bit Mach-O (`MH_EXECUTE`, `CPU_TYPE_ARM64`, `-pagezero_size 0`)
  - Size: 50,888 bytes
  - SHA-256: `b51b7c0cc31d02a469b1b6dff7029dd8413bd8592650c2ad572cef388161f9e4`
- **Build Registration:** Added `physical_page_manager.cpp` to `winios_core` and `stage194_3_physical_allocator_tests` in `CMakeLists.txt`.

### 2.2 Algorithmic Characteristics
- **Bitmap Space Efficiency:** A 256 MB DRAM physical space (16,384 pages of 16 KB) requires only 2,048 bytes of bitmap tracking memory, comfortably fitting within a single 16 KB page frame.
- **Contiguous Allocation:** The allocator supports order-$N$ / contiguous multi-page spans by scanning for run lengths of unset bits.
- **Protection & Re-use:** Previously freed pages are immediately available for subsequent allocation cycles. Misaligned addresses or out-of-bounds page frees are deterministically rejected.

---

## 3. Verification Matrix

The test suite `tests/unit/stage194_3_physical_allocator_tests.cpp` executes 5 comprehensive test suites:

| Test Case | Description | Instructions Retired | Result |
| :--- | :--- | :--- | :--- |
| `test_physical_bitmap_initialization_and_reservation` | Validates 256-page bitmap initialization, zeroing, and reservation of 64KB boot space. | N/A (Host API) | **PASS** |
| `test_page_allocation_and_free_cycles` | Validates single-page, multi-page contiguous allocation, deallocation, and reuse. | N/A (Host API) | **PASS** |
| `test_allocation_exhaustion_and_misaligned_frees` | Tests allocation rejection upon memory exhaustion, misaligned free rejection, and range checks. | N/A (Host API) | **PASS** |
| `test_guest_arm64_alloc_routine_execution` | Emits ARM64 machine code sequence into guest RAM and runs in EL1; sets bit in guest RAM. | 17 guest insns | **PASS** |
| `test_guest_physical_allocator_artifact_execution` | Loads authentic compiled Mach-O binary `guest_physical_allocator`, boots into EL1, executes full alloc-free-realloc sequence. | 262 guest insns | **PASS** |

Both **Release** and **Debug** builds pass cleanly under MSVC C++20 `/W4 /WX /permissive-`.
