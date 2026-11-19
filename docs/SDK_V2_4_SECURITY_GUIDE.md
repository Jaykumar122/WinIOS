# WinIOS SDK v2.4 Security & Application Hardening Guide

## 1. Overview and Platform Boundaries

WinIOS SDK v2.4 (`0x00020400`) establishes a hardened, capability-based application execution environment for freestanding ARM64 guest applications running on the WinIOS research platform.

> [!IMPORTANT]
> **Research Platform Disclaimer**  
> WinIOS is an experimental, Windows-hosted ARM64 guest application research and emulation environment modeling the Apple A16 Bionic (`T8120`, `iPhone15,4`, `D37AP`) architecture with 16 KB virtual memory granules. It does **not** provide physical Apple hardware execution, genuine iOS kernel compatibility, UIKit, SwiftUI, SpringBoard, or private Apple system services.

---

## 2. Process Identity and Generational Handles

### ProcessHandle Structure
Applications interact with the WinIOS supervisor through generational process handles:
```c
typedef struct {
    uint32_t pid;
    uint32_t generation;
} winios_process_handle_t;
```

### Generation Security Guarantees
- **Monotonic Generation Increment**: Each PID slot maintains a generational counter that increments monotonically on every process allocation.
- **Permanent Slot Retirement**: Slots that reach the maximum generational ceiling (`kMaxGeneration = 0xFFFFFFFE`) are permanently retired to prevent arithmetic generation wraparound.
- **Stale Handle Invalidation**: System services, IPC brokers, and document registries reject stale generation handles immediately, preventing PID recycling and use-after-free attacks.
- **CSPRNG Identity Tokens**: Each process instance is backed by a 128-bit cryptographically secure random token generated via Windows CNG (`BCryptGenRandom`).

---

## 3. Capability-Based IPC and Anti-Replay Defense

### 128-Bit CSPRNG Capability Tokens
Communication rights between applications require an explicit capability grant issued by the `IpcCapabilityBroker`.
- Tokens are indexed by a 32-character hexadecimal string representing 128 bits of CSPRNG entropy.
- Forged tokens and unregistered capability IDs are rejected with `IpcStatus::PermissionDenied`.

### Anti-Replay Monotonic Sequence Tracking
Every message transmission must provide a strictly increasing 64-bit sequence number:
- Replayed sequence numbers (`sequence_num <= last_sequence_num`) are detected and rejected with `IpcStatus::ReplayDetected`.
- Unordered or replayed frames trigger immediate channel drops and security logging.

---

## 4. Generational Document Grants

### Document Sandbox Access
Applications cannot open arbitrary files on the host or guest filesystem. Document streams must be registered in the `DocumentRegistry` within authorized virtual roots (`/Documents`, `/Shared`, `/Inbox`).

### Permission-Preserving Delegation
- Document grants bind the owner PID and the granted peer PID **together with their exact generational handles**.
- If either the granting process or the recipient process terminates, the generational handle becomes invalid. Subsequent attempts to read or write using stale handles are denied (`DocumentError::PermissionDenied`), preventing unauthorized data leakage across process lifecycles.

---

## 5. Filesystem Path Canonicalization and Jail Enforcement

All paths in manifests, CLI invocations, and package extractions are validated through `CanonicalPathValidator`:
- Absolute path prefixes (`/`, `\`, drive letters `C:`) are strictly prohibited in application packages.
- Directory traversal sequences (`..`, `../`, `..\`) are detected and rejected.
- Paths must be POSIX-normalized with forward slashes `/`.
- Component names must not exceed 255 characters, and full path length must not exceed 1024 bytes.

---

## 6. Memory Safety and Syscall Boundary

### 16 KB Granule Alignment
All guest Mach-O segments (`__TEXT`, `__DATA`, `__PAGEZERO`) must be aligned to 16,384-byte (16 KB) page boundaries matching Apple Silicon memory architectures.

### W^X Memory Protection
Memory mappings enforce strict Write-XOR-Execute (W^X):
- Executable code segments are mapped `PROT_READ | PROT_EXEC`.
- Writable data segments are mapped `PROT_READ | PROT_WRITE`.
- Segments requesting both `PROT_WRITE` and `PROT_EXEC` are strictly rejected at load time.

### Adversarial Syscall Boundary Validation
The syscall dispatcher validates user pointers and struct sizes using checked arithmetic:
- Buffer addresses within the kernel address space (`>= 0x8000000000000000ULL`) trigger immediate `EFAULT`.
- Unmapped memory or out-of-bounds pointer offsets are caught and safely reported without host process crashes.
