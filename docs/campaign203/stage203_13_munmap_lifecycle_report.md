# WinIOS Campaign 203 Stage 203.13: munmap and Mapping Lifecycle Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_13_munmap_lifecycle_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.13 implements and independently validates the complete mapping lifecycle and deallocation semantics for `munmap` and `mach_vm_deallocate` in `DarwinVmSpace`.

Key verified architectural features:
1. **Full Region Unmapping:** Complete removal of a virtual memory entry restores the region count and ensures subsequent accesses return `KERN_INVALID_ADDRESS` in read/write syscalls and `false` in access checks.
2. **Left-Edge Trimming:** Unmapping the initial portion of a region properly updates the base address (`start_va`), decrements the mapped `size`, and increments the backing object offset (`offset_in_object`), ensuring remaining pages preserve their mapped content at exact offsets.
3. **Right-Edge Trimming:** Unmapping trailing pages correctly truncates `size` while keeping initial pages intact and accessible.
4. **Middle Hole Punching / Splitting:** Unmapping a sub-range strictly inside an allocated region cleanly splits the entry into two distinct virtual memory entries (head and tail), correctly recalibrating the tail entry's object offset and invalidating only the punched hole.
5. **Strict Parameter Validation:** Enforces 16KB page alignment and non-zero length requirements, returning `KERN_INVALID_ARGUMENT` on invalid requests and `KERN_INVALID_ADDRESS` when attempting to unmap non-existent addresses.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_full_unmap` | Full mapping destruction, access revocation, read/write faults | PASS | PASS |
| `test_left_edge_unmap` | Head trimming, start address advance, offset_in_object tracking | PASS | PASS |
| `test_right_edge_unmap` | Tail trimming, truncation, preserved head data | PASS | PASS |
| `test_middle_unmap_split` | Middle entry splitting, region count increase, hole verification | PASS | PASS |
| `test_invalid_parameters` | Misaligned address, zero length, non-existent address rejection | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
