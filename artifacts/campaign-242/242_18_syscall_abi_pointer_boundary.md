# Stage 242.18: Syscall ABI, Pointer Bounds and Error Containment

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Audited the syscall boundary and dispatch error handling:

- **Darwin XNU ABI Conformance**: Syscall errors set the Carry flag (`PSTATE.C = 1` / `cpu.get_nzcv() & 0x2u`) and return positive errno in X0.
- **Pointer Boundary Checks**: Passing invalid pointers (e.g. NULL pointer or unmapped buffer) to `write` fails safely with `EFAULT` (14).
- **Process Isolation**: Syscall failure is strictly contained within the issuing process and does not corrupt the host or peer processes.
- **Test Target**: `stage242_18_syscall_pointer_boundary_tests` (PASSED).
