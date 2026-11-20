# Stage 241.13: Syscall Registry and ABI Negotiation Audit

## 1. Syscall Registry Integration
- Canonical registry defined in `src/kernel/syscall_registry.h` (`kCanonicalSyscallRegistry`).
- Maps syscall numbers, categories, argument counts, memory modification flags, and minimum required ABI versions.
- **Category C ABI Negotiation (Syscall 507)**:
  - Supports negotiation to v1.1 (`0x00010100`).
  - Downgrade prevention enforced: attempts to downgrade negotiated ABI version return `-EPERM`.
  - Unknown syscalls return `-ENOSYS`.
