# WinIOS Guest Application Startup Contract (ABI v1.5)

**Specification Version:** 1.5.0  
**Effective Campaign:** 221  
**Architecture:** ARM64 (AArch64 EL0)  
**Granule:** 16 KB Page Translation  

---

## 1. Entry-Point Expectations & Execution Environment

1. **CPU Execution Level:** Execution commences strictly in Exception Level 0 (**EL0t**).
2. **Translation:** Stage 1 MMU active with process-dedicated ASID (1..255). 16 KB translation granule.
3. **Entry Vector:** Execution branches to the virtual address declared in the Mach-O `LC_MAIN` entry point (or `LC_UNIXTHREAD` thread state).
4. **Return Address:** Register `X30` (LR) is initialized to `0x0`. Returning directly from the entry point without invoking `exit()` triggers an unhandled return trap.

---

## 2. Initial Register State (AAPCS64 & WinIOS Conventions)

Upon arrival at the guest application entry point, CPU architectural registers are initialized as follows:

| Register | Value | Description |
|---|---|---|
| `X0` | `uint64_t` | Guest virtual address of `GuestAppArgBlock` (or `argc` for standard BSD entry). |
| `X1` | `uint64_t` | Guest virtual address of `char* argv[]` NULL-terminated array. |
| `X2` | `uint64_t` | Guest virtual address of `char* envp[]` NULL-terminated array. |
| `X3`–`X28` | `0x0` | Initialized to zero to prevent information leakage across processes. |
| `X29` (FP) | `0x0` | Frame pointer initialized to zero (top of call stack). |
| `X30` (LR) | `0x0` | Link register initialized to zero. |
| `SP_EL0` | `uint64_t` | 16-byte aligned user stack pointer (`(stack_base + stack_size) & ~0xF`). |
| `PSTATE` | `0x00000000` | User mode (EL0t), standard condition flags (NZCV=0), interrupts unmasked. |
| `TPIDR_EL0`| `uint64_t` | Thread-local storage pointer (or 0 if uninitialized). |

---

## 3. Argument Block: `GuestAppArgBlock` (v1.5)

The `GuestAppArgBlock` structure (96 bytes) is mapped in readable guest memory and passed via `X0`:

```c
#pragma pack(push, 8)
struct GuestAppArgBlock {
    uint32_t contract_version;    // 0x00010500 (v1.5) or 0x00010200 (v1.2 backward compatible)
    uint32_t process_id;          // Guest Process ID (PID)
    uint64_t app_base_address;    // Base address where Mach-O is mapped
    uint64_t stack_base;          // Stack bottom virtual address
    uint64_t stack_size;          // Allocated stack size in bytes
    uint64_t heap_base;           // Heap starting address (16 KB aligned)
    uint64_t heap_limit;          // Heap maximum expansion address
    uint32_t argc;                // Argument count
    uint32_t envc;                // Environment variable count
    uint64_t argv_ptrs;           // Guest pointer to argv array
    uint64_t envp_ptrs;           // Guest pointer to envp array
    uint32_t initial_window_id;   // Default window ID created for app (or 0)
    uint32_t bundle_storage_id;   // Isolated storage partition ID
    uint32_t reserved[4];         // Padding for ABI alignment
};
#pragma pack(pop)
```

---

## 4. Initial Stack Layout

The user stack grows downward from `stack_top_va`. Prior to entry, the loader formats the stack layout:

```
[High Memory: stack_top_va]
  +---------------------------------------+
  | Environment string pool ("KEY=VAL\0") |
  +---------------------------------------+
  | Argument string pool ("arg0\0", ...)  |
  +---------------------------------------+
  | NULL pointer (envp terminator)        |
  +---------------------------------------+
  | envp[envc-1] pointer                  |
  | ...                                   |
  | envp[0] pointer                       |
  +---------------------------------------+
  | NULL pointer (argv terminator)        |
  +---------------------------------------+
  | argv[argc-1] pointer                  |
  | ...                                   |
  | argv[0] pointer                       |
  +---------------------------------------+
  | argc (uint64_t)                       |
  +---------------------------------------+ <-- SP_EL0 (16-byte aligned)
[Low Memory: stack_bottom_va]
```

---

## 5. Application Termination Contract

1. An application terminates by invoking `SYS_exit` (syscall number 1) via `svc #0x80`:
   - `X16` = 1 (`SYS_exit`).
   - `X0`  = Exit status code (`int32_t`).
2. The kernel transitions the process state to `Terminated` / `Zombie`.
3. Open file descriptors, active window surfaces, timers, and non-persistent IPC ports are automatically reclaimed.
4. The parent process is notified and retrieves the exit code via `SYS_wait4` (syscall number 7).
