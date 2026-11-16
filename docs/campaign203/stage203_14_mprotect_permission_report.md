# WinIOS Campaign 203 Stage 203.14: mprotect and Permission Faults Report

**Date:** 2026-11-16  
**Status:** COMPLETE (PASS)  
**Target Device Identity:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`)  
**Test Suite:** `stage203_14_mprotect_permission_tests` (Release & Debug)

---

## 1. Executive Summary

Stage 203.14 implements and independently validates virtual memory protection transitions and permission violation faults across `mprotect` and `mach_vm_protect` within `DarwinVmSpace`.

Key verified architectural features:
1. **Full Region Protection Modulation:** Transitions between Read-Write (RW) and Read-Only (RO) succeed and instantly update enforcement. Write attempts into RO regions correctly return `KERN_PROTECTION_FAILURE`.
2. **Subrange Splitting on Protection Boundaries:** Protecting a subset of pages within a region cleanly splits the enclosing VM entry at boundary addresses, allowing distinct protection flags across adjacent pages without corrupting offset tracking.
3. **Apple Silicon W^X Policy Enforcement:** Setting simultaneous Write and Execute flags (`PROT_WRITE | PROT_EXEC`) is strictly rejected with `KERN_PROTECTION_FAILURE` across all protection APIs.
4. **Mach VM Maximum Protection Ceiling (`set_maximum`):** Calling `mach_vm_protect` with `set_maximum=true` adjusts the entry's `max_prot` ceiling and clamps `prot`. Subsequent requests attempting to elevate permissions beyond `max_prot` are rejected with `KERN_PROTECTION_FAILURE`.
5. **Parameter & Range Validation:** Invalid arguments (misaligned addresses, zero length) return `KERN_INVALID_ARGUMENT`. Unmapped or partially unmapped target ranges return `KERN_INVALID_ADDRESS`.

---

## 2. Test Execution Matrix

| Test Case | Description | Result (Release) | Result (Debug) |
|---|---|---|---|
| `test_full_region_mprotect` | Full region RW -> RO -> RW modulation, access check validation | PASS | PASS |
| `test_partial_subrange_mprotect_split` | Middle page protection change, boundary entry splitting, per-page protection | PASS | PASS |
| `test_wx_policy_rejection` | Rejection of Write+Execute across mprotect and mach_vm_protect | PASS | PASS |
| `test_mach_vm_protect_set_maximum` | set_maximum=true ceiling enforcement, prot clamping, elevated prot rejection | PASS | PASS |
| `test_invalid_parameters` | Misaligned address, zero length, unmapped target range rejection | PASS | PASS |

---

## 3. Verification Details

All tests compiled under `/W4 /WX /permissive-` with 0 warnings and executed with 0 failures in both Release and Debug configurations.
