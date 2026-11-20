# Stage 238.22: Cross-Subsystem Integration

## 1. Overview
Stage 238.22 audited the interactions between subsystems:
- Syscall dispatching & ABI negotiation -> Kernel Process Management.
- Process Identity & Handle Generation -> IPC Capability Broker.
- User Address Space & Policy -> Guest Memory Validation.
- VFS Path Canonicalization -> Package Installer & WAL State Machine.
- Desktop Session Manager -> UI Window Lifecycle.

## 2. Integration Hardening Summary
1. **ABI Negotiation to Syscall Routing**:
   - Calling syscall 507 smoothly transitions process Category C ABI from v1.0 to v1.1.
   - Syscalls 508..512 transition from returning `ENOSYS` to validating handles and returning `EBADF` / `EFAULT`.
2. **Generational Process Handles in IPC**:
   - Recycling a terminated PID increments generation.
   - Any message dispatched using a pre-existing token returns `StaleProcessHandle` cleanly without misrouting messages to the newly allocated process.
3. **Address-Space Confinement in Syscalls**:
   - Setting `AddressSpacePolicy::StrictSandbox` (2 GiB) on a process confines all syscall pointer validations within `[0, 2 GiB)`.
4. **VFS Canonicalization in Package Installer**:
   - All bundle asset paths are validated via `CanonicalPathValidator::validate_and_normalize_bundle_path` before WAL extraction, blocking traversal attacks and device names.
