# Stage 238.6: Window Creation and Guest Pointer Safety

## 1. Overview & Syscall 500 Audit
Stage 238.6 audits pointer validation semantics during window creation (`kSysWiniosWindowCreate`, syscall 500), specifically verifying that guest virtual pointers (`title_va`) cannot bypass memory safety checks.

## 2. Technical Findings & Enforcement Architecture

### 2.1 Pointer Validation Semantics
- **Null Pointer (`title_va == 0`)**: Cleanly and safely defaults to `"WinIOS App"` without error.
- **Valid User Pointer**: Read via `GuestMemoryValidator::copy_string(uas, title_va, max_len, title)` within bounds; copies title cleanly.
- **Invalid Non-Null Pointer (Unmapped, Out-of-bounds, Non-terminated)**:
  - Previously in Campaign 236, failing string copies could lead to ambiguous fallback.
  - Now strictly and atomically rejected with `kErrnoEFAULT` (`14`).
  - Increments `stats_.total_invalid_pointers`.
  - Atomicity guaranteed: No window object is created in `WindowManager`, no surface is retained in `SurfaceResourceManager`, and no display resources leak.
- **Zero Dimensions (`w == 0 || h == 0`)**: Strictly rejected with `kErrnoEINVAL` (`22`).

## 3. Adversarial Test Results (`stage238_6_window_creation_pointer_safety_tests`)
1. Null pointer (`title_va == 0`) -> Created window with title `"WinIOS App"` (100% PASS).
2. Valid string -> Created window with `"My Custom Window Title"` (100% PASS).
3. Unmapped pointer (`0x88880000`) -> Rejected with `EFAULT`, window count == 0 (100% PASS).
4. Out-of-bounds pointer (`> 32 GiB`) -> Rejected with `EFAULT` (100% PASS).
5. Zero dimensions (`w=0` or `h=0`) -> Rejected with `EINVAL` (100% PASS).
