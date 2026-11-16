# WinIOS Campaign 203: Architectural Support Matrix

**Device Research Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Physical Topology:** 6 physical cores (2 Everest Performance + 4 Sawtooth Efficiency)  
**Virtual Topology:** 1 to 8 virtual ARM64/ARM64e execution cores  
**Page Size:** 16,384 bytes (16KB Apple page granule, mask `0x3FFF`)  
**Address Spaces:** 48-bit canonical userspace (`0x0000000000000000` - `0x00007FFFFFFFFFFF`) and high kernel virtual address space  

---

## 1. Subsystem Capability Matrix

| Architectural Subsystem | Status / Capability | Architectural Specification & Implementation Details |
|---|---|---|
| **Virtual Memory Management** | Fully Supported | 16KB Apple page granule; `DarwinVmSpace` object model; anonymous zero-fill; subrange entry splitting on partial unmap/protect; Copy-On-Write break on fork. |
| **POSIX `mmap` / `munmap`** | Fully Supported | `MAP_FIXED` exact placement; `MAP_ANON` zero-fill; hint fallback on non-fixed collisions; full, left-edge, right-edge, and middle split unmapping creating unmapped holes. |
| **`mprotect` & W^X Enforcement** | Fully Supported | RW, RO, RX transitions supported; Apple Silicon W^X policy strictly enforced (RWX rejected unless `MAP_JIT` set); maximum protection ceiling enforced via `mach_vm_protect`. |
| **Mach VM Services (`mach_vm_*`)** | Fully Supported | `mach_vm_allocate` (`kDarwinVmFlagsAnywhere` and non-destructive `kDarwinVmFlagsFixed`), `mach_vm_deallocate`, `mach_vm_protect`, `mach_vm_read`, `mach_vm_write`. |
| **Shared Memory Backing** | Fully Supported | Inter-process shared memory objects (`MAP_SHARED`); concurrent multi-process mapping; shared objects preserved across fork. |
| **Mach-O Load Command Parser** | Fully Supported | `LC_SEGMENT_64`, `LC_BUILD_VERSION`, `LC_MAIN`, `LC_DYLD_EXPORTS_TRIE`, `LC_DYLD_CHAINED_FIXUPS`, `LC_LOAD_DYLINKER`, `LC_LOAD_DYLIB`, `LC_REEXPORT_DYLIB`, `LC_LOAD_WEAK_DYLIB`. |
| **Dyld Dependency Graph** | Fully Supported | Multi-image topological resolution; Tarjan strongly connected components (SCC) cycle detection; upward dependencies; transitive re-export delegation; missing weak dependency resilience. |
| **Two-Level Namespace Symbols** | Fully Supported | Ordinal-based library lookup; transitive re-export symbol delegation; flat namespace fallback; weak symbols bind to 0 (NULL). |
| **Chained Fixups Engine** | Fully Supported | `DYLD_CHAINED_PTR_ARM64E` and `DYLD_CHAINED_PTR_ARM64E_AUTH`; page-table delta walking; auth rebase and bind decoding. |
| **Pointer Authentication (PAC)** | Supported (PRF) | ARM64e PAC authentication verified using 3-round SipHash PRF producing 16-bit tags; IA/IB/DA/DB keys with address diversification. |
| **Static Initializers** | Fully Supported | `__mod_init_func` sections; strict topological dependency sequencing; idempotency (run-once protection). |
| **Mach IPC Port Sets** | Fully Supported | `kMachPortRightPortSet` handle type; `insert_set_member` and `extract_set_member`; message receipt multiplexing; local port attribution on receive (`msgh_local_port` receives actual port name). |
| **Dead-Name Notifications** | Fully Supported | `MACH_NOTIFY_DEAD_NAME` (72) asynchronous notifications generated and queued to registered notification ports upon target port destruction. |
| **Mach Message Descriptors** | Fully Supported | Complex messages (`MACH_MSGH_BITS_COMPLEX`); Port Descriptors (`MACH_MSG_TYPE_MAKE_SEND`, `MACH_MSG_TYPE_MOVE_RECEIVE`); Out-Of-Line (OOL) memory descriptors with sender deallocation and receiver placement. |
| **Mach IPC Timeout & Queue Limits** | Fully Supported | Port queue limit threshold (`kMaxQueueLimit = 64`); send timeout (`kMachSendTimedOut = 0x10000004`); receive timeout (`kMachRcvTimedOut = 0x10004003`); dead destination cancellation (`kMachSendInvalidDest = 0x10000003`); atomic send-and-receive pseudo-receive preservation. |
| **Darwin Syscall Dispatcher** | Fully Supported | Unified `SVC #0x80` dispatching; positive `X16` to BSD handlers; negative `X16` to Mach traps; bound `DarwinVmSpace` and `MachIpcSubsystem`. |
| **Process Startup & ABI** | Fully Supported | ARM64 Darwin ABI stack frame: 16-byte aligned SP; `[SP]` argc; null-terminated `argv[]`, `envp[]`, `apple[]` arrays; automatic apple vector (`executable_path`, `stack_guard`, `malloc_entropy`, `pfz`); registers `SP`, `PC`, `X0..X3`. |
| **TLS & Thread Runtime** | Fully Supported | Architectural system registers: `TPIDRRO_EL0` read-only at EL0 (holds `pthread_t`); `TPIDR_EL0` read/write at EL0 (user TLS); 184-byte `DarwinPthreadHeader` with 16 TSD slots; Mach-O TLV descriptor resolution. |
| **EL2 Stage-2 MMU Syndrome** | Fully Supported | Syndrome HPFAR decoding with correct mask `0x000000FFFFFFFFF0ULL` (40-bit IPA address bits [47:4] shifted by 8). |
| **DMA Virtual Engine** | Fully Supported | Direct contiguous memory block transfer fast-path (`std::memmove`) providing 580x to 1335x measured throughput over byte loop emulation. |

---

## 2. Invariant & Sandboxing Guarantees

1. **W^X Policy:** Direct `mmap` and `mprotect` calls requesting simultaneous write and execute permissions (`PROT_WRITE | PROT_EXEC`) without `MAP_JIT` return failure (`-1`), preventing non-JIT executable memory creation.
2. **TPIDRRO_EL0 Privilege Sandboxing:** Userspace EL0 code attempting to write `TPIDRRO_EL0` via `MSR` is blocked and raises an architectural exception, ensuring thread pointers remain protected from userspace tampering.
3. **Mach Port Handle Lifetime:** All port handles encode generation IDs; deallocating and reallocating a slot increments generation, preventing stale handle use.
