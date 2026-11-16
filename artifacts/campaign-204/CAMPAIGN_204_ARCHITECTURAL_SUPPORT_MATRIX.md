# WinIOS Campaign 204 — Architectural Support Matrix

**Campaign:** 204  
**Date:** November 16, 2026  
**Architecture:** ARM64 / ARM64e (ARMv8.0-A / ARMv8.1-A / ARMv8.3-A PAC / Apple A16 Bionic T8120 Profile)  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 / MSVC C++20 (`/W4 /WX /permissive-`)  

---

## 1. Architectural Feature Coverage

| Subsystem | Architectural Feature | Implementation Level | Verification Evidence |
|---|---|---|---|
| **Instruction Set** | General-Purpose Arithmetic (`add`, `sub`, `cmp`, `neg`) | Full (32-bit & 64-bit) | `stage204_18_instruction_coverage_tests.cpp` |
| **Instruction Set** | Bitwise & Shifts (`and`, `orr`, `eor`, `lsl`, `lsr`, `asr`) | Full | `stage204_18_instruction_coverage_tests.cpp` |
| **Instruction Set** | PC-Relative Addressing (`adr`, `adrp`) | Full (±1MB / ±4GB range) | `adr_adrp_conformance_tests.cpp`, `stage204_18` |
| **Instruction Set** | Immediate Load (`movz`, `movk`, `movn`) | Full | `stage204_18_instruction_coverage_tests.cpp` |
| **Instruction Set** | Memory Access (`ldr`, `str`, `ldrb`, `strb`, `ldrh`, `strh`) | Full | `stage204_18_instruction_coverage_tests.cpp` |
| **Instruction Set** | Branching (`b`, `bl`, `br`, `blr`, `ret`, `b.cond`, `cbz`, `cbnz`) | Full | `stage204_18_instruction_coverage_tests.cpp` |
| **Instruction Set** | Exception Generation (`svc #0x80`, `svc #0`) | Full (Darwin BSD & Mach syscall ABI) | `stage204_14`, `stage204_18`, `stage204_19` |
| **Instruction Set** | Pointer Authentication (ARMv8.3-A PAC) | Architectural Stubs & Invariants | `cpu/pointer_auth.h`, `stage201_3_pac_tests.cpp` |
| **Virtual Memory** | Page Granule Size | 16KB (`0x4000`) Architectural Standard | `DarwinVmSpace`, `stage204_5` |
| **Virtual Memory** | Memory Protection Attributes | `VM_PROT_READ`, `VM_PROT_WRITE`, `VM_PROT_EXECUTE` | `DarwinVmSpace::mprotect`, `stage204_9` |
| **Virtual Memory** | W^X Enforcement | Strict (Segments cannot be simultaneously W & X) | `stage204_5_segment_mapping_vm_tests.cpp` |
| **Virtual Memory** | Post-Relocation Finalization | `__DATA_CONST` transition to `R--` | `UserspaceMachOLoader::finalize_protections` |
| **Mach-O Loader** | Header Parsing | `mach_header_64`, `CPU_TYPE_ARM64`, `MH_EXECUTE`, `MH_DYLIB` | `UserspaceMachOLoader::parse_metadata` |
| **Mach-O Loader** | Load Command Support | `LC_SEGMENT_64`, `LC_DYLD_INFO_ONLY`, `LC_LOAD_DYLIB`, `LC_RPATH`, `LC_MAIN`, `LC_DYLD_CHAINED_FIXUPS` | `macho_fixture_corpus.cpp`, `stage204_4` |
| **Dynamic Loader** | Dyld Binding Architecture | Two-Level Namespace, Flat Lookup, Self Ordinal, Weak | `DyldEngine::resolve_symbol_two_level`, `stage204_7` |
| **Dynamic Loader** | Export Trie | Bytecode Trie Traversal, Cycle Detection, Depth Limits | `DyldEngine::parse_export_trie`, `stage204_6` |
| **Dynamic Loader** | Search Path Expansion | `@rpath`, `@loader_path`, `@executable_path` | `DyldEngine::resolve_rpath`, `stage204_8` |
| **Dynamic Loader** | Module Initialization Order | Bottom-up topological sort on DAG with cycle rejection | `DyldEngine::compute_topological_initializers`, `stage204_10` |
| **Darwin ABI** | Process Startup Stack | `argc`, `argv[]`, `envp[]`, `apple[]` auxv strings | `DarwinProcessStartup::setup_initial_stack`, `stage204_15` |
| **Darwin ABI** | Thread-Local Storage (TLS) | Synthetic TLS blocks, TPIDR_EL0 register assignment | `DarwinProcessStartup::setup_tls`, `stage204_15` |
| **Darwin ABI** | libSystem Compatibility | 25+ essential C runtime stubs (`_printf`, `_malloc`, `_free`, `___error`, `___stack_chk_fail`) | `DarwinRuntimeLibrary`, `stage204_11` |
| **Darwin Kernel** | Mach IPC Subsystem | Generation-safe port handles, port rights, port sets | `MachPortSpace`, `stage204_3` |
| **Darwin Kernel** | Virtual Filesystem (VFS) | In-memory vnode tree, `/dev/null`, `/dev/zero`, `/dev/random`, mounts, symlinks | `DarwinBsdSubsystem`, `stage204_12` |
| **Darwin Kernel** | File Descriptors | Per-process fd table, dup, lseek, read, write, cloexec | `DarwinBsdSubsystem`, `stage204_13` |
| **Darwin Kernel** | BSD Syscalls | 20+ syscalls (`SYS_exit`, `SYS_read`, `SYS_write`, `SYS_open`, `SYS_close`, `SYS_mprotect`, `SYS_munmap`, `SYS_getpid`, `SYS_sysctl`) | `DarwinSyscallDispatcher`, `stage204_14` |
| **Darwin Concurrency** | POSIX Threads | `pthread_create`, `pthread_exit`, `pthread_join`, thread registers, stack management | `DarwinThreadRuntime`, `stage204_16` |
| **Darwin Concurrency** | Synchronization Primitives | `pthread_mutex_t`, `pthread_cond_t`, futex emulation | `DarwinSyncPrimitives`, `stage204_17` |

---

## 2. Invariants & Guarantees
1. **Granule Invariant:** All segment boundaries, mmap allocations, and memory protections adhere strictly to the 16KB (`0x4000`) page size required by ARM64 Darwin.
2. **Security Invariant:** Loader and runtime reject path traversal attempts (`../`), recursion loops in export tries (> 64), symlink cycles (> 16), and conflicting segment mappings.
3. **Reproducibility Invariant:** All tests and benchmarks produce deterministic execution across host threads and across clean checkouts.
