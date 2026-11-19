# Stage 232.15 — Syscall Conformance & Memory Validation Suite Evidence

**Campaign:** 232  
**Stage:** 232.15  
**Component:** `tests/unit/stage232_15_syscall_memory_tests.cpp`  
**Test Executable:** `build/RelWithDebInfo/stage232_15_syscall_memory_tests.exe`  
**CTest Target:** Test #933 (`stage232_15_syscall_memory_tests`)

---

## 1. Test Suite Architecture

Stage 232.15 verifies both static compile-time ABI conformance and dynamic runtime memory safety across the guest syscall boundary.

### 1.1 Compile-Time ABI Conformance (`static_assert`)
Verified exact bit-for-bit field layouts, alignments, and sizes matching freestanding ARM64 Mach-O EL0 ABI:
- `winios_ipc_msg_desc_t` (24 bytes)
- `winios_ipc_msg_v2_t` (32 bytes)
- `winios_msg_header_t` (24 bytes)
- `winios_settings_snapshot_t` (48 bytes)
- `winios_task_entry_t` (120 bytes)
- `winios_accessibility_info_t` (32 bytes)
- `winios_diag_event_record_t` (48 bytes)
- `winios_diag_query_req_t` (32 bytes)
- `winios_app_info_t` (128 bytes)
- `winios_app_list_req_t` (24 bytes)

### 1.2 Runtime Memory Validation Test Scenarios
1. **Null Pointer Rejection:** Verifies `validate_buffer`, `validate_guest_read`, `validate_guest_write`, `copyin`, `copyout`, and `copy_string` return `ValidationResult::NullPointer` when passed address 0.
2. **Integer Overflow & Wrap-Around:** Verifies `va = 0xFFFFFFFFFFFFFFF0ULL` with `size = 32` correctly triggers `ValidationResult::IntegerOverflow`.
3. **Kernel/EL1 High-Address Protection:** Verifies attempts to access memory at or above `kUserSpaceLimitVa` (`0x0001000000000000ULL`) fail with `ValidationResult::OutOfBounds`.
4. **Unmapped Page Defense:** Verifies access to unmapped page addresses returns `ValidationResult::UnmappedPage`.
5. **Read-Only Page Write Defense:** Verifies that attempting to copyout or write to a page mapped as `UserRead` returns `ValidationResult::PermissionDenied`, while read operations succeed.
6. **SEC-232-05 String Termination Defense:** Verifies that passing a 1024-byte buffer without a null terminator returns `ValidationResult::StringNotTerminated`.
7. **Cross-Page Spanning Buffers:** Verifies that a buffer spanning across a 16 KB page boundary from a mapped page into an unmapped page triggers `ValidationResult::UnmappedPage`, and when the second page is read-only, write operations are rejected with `ValidationResult::PermissionDenied`.

---

## 2. Verification Log Output

```text
========================================================
STAGE 232.15: Syscall Copyin/Copyout Memory Validation Tests
========================================================
[Stage 232.15] Testing null pointer validation across all validation primitives...
[Stage 232.15] Testing address space limits and integer overflow...
[Stage 232.15] Testing unmapped pages and read-only permission enforcement...
[Stage 232.15] Testing SEC-232-05: String null termination defense...
[Stage 232.15] Testing multi-page boundary crossing and partial unmapped rejection...
All Stage 232.15 tests passed successfully!
```

CTest Result:
```text
    Start 933: stage232_15_syscall_memory_tests
9/9 Test #933: stage232_15_syscall_memory_tests ............   Passed    0.03 sec
```
Status: **PASSED (100%)**
