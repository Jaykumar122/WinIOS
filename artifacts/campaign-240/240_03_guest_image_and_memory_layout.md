# Stage 240.3: Guest Image and Memory Layout

## 1. Memory Map Specification
WinIOS implements a deterministic virtual address space model for ARM64 guest applications:

| Virtual Address Range | Size | Region | Permissions | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0x00000000 - 0x0000FFFF` | 64 KB | `__PAGEZERO` | None (`---`) | Unmapped guard page for NULL-pointer trapping |
| `0x00010000 - 0x0001FFFF` | 64 KB | `__TEXT` | `R-X` | Executable code and read-only constants |
| `0x00020000 - 0x0002FFFF` | 64 KB | `__DATA` | `RW-` | Mutable global variables, BSS, zero-fill |
| `0x00030000 - 0x0004FFFF` | 128 KB | Heap | `RW-` | Dynamic memory arena (`malloc`/`free`) |
| `0x00050000 - 0x0005FFFF` | 64 KB | Guard Region | None (`---`) | Stack-heap separation guard |
| `0x00060000 - 0x0007EFFF` | 124 KB | Stack | `RW-` | Execution call stack, grows downward |
| `0x0007F000 - 0x0007FFFF` | 4 KB | Stack Guard | None (`---`) | Stack overflow detection guard page |

## 2. Validation & Security Guarantees
- Stack top initialized at `0x0007F000`, 16-byte aligned.
- Overlapping segments are rejected during load validation.
- Non-writable text segments prevent in-memory code modification attacks.
