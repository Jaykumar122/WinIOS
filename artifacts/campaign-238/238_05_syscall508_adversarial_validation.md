# Stage 238.5: Syscall 508 Adversarial Validation

## 1. Overview & Adversarial Vectors
Stage 238.5 subjects syscall 508 (`kSysWiniosWindowGetGeometry`) to adversarial validation across both Category C ABI versions, boundary conditions, and concurrent handle lifecycle operations.

## 2. Test Matrix & Results

| Vector ID | Description | ABI Level | Tested Input | Expected Result | Actual Result |
|---|---|---|---|---|---|
| **V1** | Unnegotiated ABI v1.0 Query | v1.0 (`0x00010000`) | `wid = 1`, `geom_va = 0x20000` | `kErrnoENOSYS` | `kErrnoENOSYS` (PSTATE.C=1) |
| **V2** | Missing Window Handle Query | v1.1 (`0x00010100`) | `wid = 9999` (non-existent) | `kErrnoEBADF` | `kErrnoEBADF` (PSTATE.C=1) |
| **V3** | Cross-Process Unauthorized Query | v1.1 (`0x00010100`) | PID 202 querying PID 201 window | `kErrnoEBADF` | `kErrnoEBADF` (PSTATE.C=1) |
| **V4** | Null Pointer VA | v1.1 (`0x00010100`) | `geom_va = 0x0` | `kErrnoEFAULT` | `kErrnoEFAULT` |
| **V5** | Unmapped User Address | v1.1 (`0x00010100`) | `geom_va = 0x90000000ULL` | `kErrnoEFAULT` | `kErrnoEFAULT` |
| **V6** | Misaligned Pointer | v1.1 (`0x00010100`) | `geom_va = 0x20003` (unaligned 8B) | `kErrnoEFAULT` | `kErrnoEFAULT` |
| **V7** | Valid Handle & Mapped Buffer | v1.1 (`0x00010100`) | Valid `wid`, mapped 8B-aligned VA | `0` (Success) | `0` (PSTATE.C=0, geometry verified) |
| **V8** | Destroyed Handle Query | v1.1 (`0x00010100`) | Query on destroyed `wid` | `kErrnoEBADF` | `kErrnoEBADF` |

## 3. Findings & Resolution
- **Defect Closed**: The Campaign 237 hack returning `ENOSYS` when `!win` was eradicated. Legitimate queries against closed or non-existent handles now strictly report `EBADF`.
- **Validation Suite**: Implemented and executed via `tests/unit/stage238_5_syscall508_adversarial_tests.cpp`. 100% passed.
