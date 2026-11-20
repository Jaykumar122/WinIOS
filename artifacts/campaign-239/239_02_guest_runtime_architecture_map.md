# Stage 239.2: Guest Runtime Architecture Map

## 1. End-to-End Application Execution Path

The WinIOS research runtime models a freestanding ARM64 guest application platform. The complete execution pipeline spans nine distinct phases across kernel, packaging, memory, loader, runtime, and presentation subsystems:

```
+----------------------------------------------------------------------------------------------------+
| 1. Package Admission & Installation                                                                |
|    - AppPackage (.wiospkg) -> CanonicalPathValidator -> Manifest Parsing                          |
|    - WIOSAUTH / Ed25519 signature verification -> 11-State WAL Installation -> ApplicationRegistry |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 2. Process Creation & Identity                                                                     |
|    - ProcessIdentityManager::allocate_process -> ProcessHandle { pid, generation }                  |
|    - BsdProcess created in BsdProcessManager -> Associated with parent task & credentials          |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 3. Memory & Address-Space Allocation                                                               |
|    - UserAddressSpace instantiated with AddressSpacePolicy (StandardGeneral 32GB / StrictSandbox 2GB)|
|    - PageDirectory / Translation tables populated (16 KB granule, ARM64 VMSA EL0/EL1)              |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 4. Executable Loading (Mach-O)                                                                     |
|    - MachoUserLoader parses LC_SEGMENT_64 (__TEXT, __DATA, __LINKEDIT)                             |
|    - Enforces W^X page permissions (no pages simultaneously writeable and executable)              |
|    - GuestStack staged with argc, argv, environ, and apple_auxv (EL0 sp alignment: 16 bytes)       |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 5. Runtime Initialization & ABI Handshake                                                          |
|    - Guest entry handoff: PC set to entry_point, SP set to user stack top                          |
|    - Category C ABI negotiated via Syscall 507 (kSysWiniosGetAbiVersion)                           |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 6. Syscall Dispatch & Memory Confinement                                                           |
|    - SVC #0 traps into UserSyscallDispatcher                                                       |
|    - GuestMemoryValidator checks user addresses against uas.user_space_limit()                     |
|    - Kernel verifies caller PID and handle generation                                              |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 7. UI & Event Loop                                                                                 |
|    - WindowManager allocates Window handle, bounds, z-order, surface buffers                       |
|    - GuestInputServices / InputRouter delivers keyboard, pointer, and lifecycle events             |
|    - Syscall 504 (kSysWiniosPollEvent) retrieves queued events                                     |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 8. Session Controller & Lifecycle Transitions                                                      |
|    - MultiAppSessionController tracks Active / Foreground / Background / Suspended states          |
|    - Suspended apps release CPU execution time and preserve memory/window snapshots                |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
| 9. Termination & Resource Reclamation                                                              |
|    - Exit syscall / fault / session termination                                                    |
|    - BsdProcess transitions to Zombie; FD table closed                                             |
|    - WindowManager tears down surfaces; IpcCapabilityBroker invalidates stale handles              |
|    - Process slot generation incremented in ProcessIdentityManager to prevent PID aliasing         |
+----------------------------------------------------------------------------------------------------+
```

## 2. Ownership Boundaries & Lifecycle Responsibilities

1. **Host vs Guest Boundary**:
   - Host kernel structures (`BsdProcess`, `KernelTask`, `UserAddressSpace`) own host memory allocations.
   - Guest code runs under emulation in CPU EL0; cannot access raw host pointers or physical memory.
   - All guest-supplied pointers are validated by `GuestMemoryValidator` within the bounds of `UserAddressSpace::user_space_limit()`.
2. **Process Identity Ownership**:
   - `ProcessIdentityManager` is the single source of truth for PID-to-generation bindings.
   - No subsystem retains a raw PID without an associated generation check.
3. **Session & Window Ownership**:
   - `WindowManager` owns window surface buffers and visual clipping.
   - Windows are owned by the creating PID; cross-process manipulation is rejected with `EBADF`.
