# Stage 243.17: Darwin Syscall ABI and NZCV Carry Flag Conformance

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Verified Darwin XNU syscall ABI error conventions:

- **Error Path**: Syscall errors assert the Carry flag (`cpu.get_nzcv() & 0x2u != 0`) and return positive errno in X0 (e.g. `EFAULT = 14`).
- **Success Path**: Successful syscalls clear the Carry flag and return values in X0.
- **Failure Containment**: Failing syscalls cannot alter peer process memory or crash the host runtime.
- **Test Target**: `stage243_17_darwin_syscall_nzcv_conformance_tests` (PASSED).
