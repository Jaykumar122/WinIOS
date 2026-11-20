# Stage 240.16: Guest Application Service Integration

## 1. Platform Services Integration
- Syscall boundary provides controlled access to platform services:
  - Process identification (`getpid`, `getuid`).
  - ABI negotiation and version reporting.
  - Document stream and IPC channel management.
- Boundary security:
  - All guest memory pointers passed as arguments are validated against caller's VFS sandbox and task address space.
  - Unauthorized service requests return appropriate POSIX error codes (`-EPERM`, `-EACCES`).
