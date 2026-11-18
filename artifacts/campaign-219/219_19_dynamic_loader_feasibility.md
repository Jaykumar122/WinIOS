# WinIOS Campaign 219 — Stage 219.19: Dynamic Mach-O Dependency Loader Feasibility Audit

## 1. Executive Summary

This forensic audit evaluates the technical feasibility of executing dynamically linked 64-bit ARM64 Mach-O binaries in the WinIOS research environment.

While previous campaigns established genuine freestanding static Mach-O loading (`LC_SEGMENT_64`, `LC_UNIXTHREAD`, `LC_MAIN`), Darwin userspace binaries frequently rely on shared libraries (`.dylib`) and dynamic linking (`dyld`).

This audit specifies the exact requirements, analyzes the complexity trade-offs between full Apple `dyld` and a kernel-orchestrated bounded dynamic loader, and defines the architecture for Stage 219.20.

---

## 2. Mach-O Dynamic Linking Structure & Load Commands

A dynamically linked 64-bit ARM64 Mach-O binary includes specialized load commands:

```
+-------------------------------------------------------------------------+
| Mach-O Header 64-bit: magic=0xFEEDFACF, cputype=ARM64, filetype=MH_EXEC |
+-------------------------------------------------------------------------+
| LC_SEGMENT_64 (__PAGEZERO: vmaddr=0x0, vmsize=0x100000000)              |
| LC_SEGMENT_64 (__TEXT: vmaddr=0x100000000, prot=r-x)                   |
| LC_SEGMENT_64 (__DATA: vmaddr=0x100004000, prot=rw-)                   |
|   - Section __got (Global Offset Table)                                 |
|   - Section __la_symbol_ptr (Lazy symbol pointers)                      |
| LC_SEGMENT_64 (__LINKEDIT: vmaddr=0x100008000, prot=r--)               |
| LC_LOAD_DYLINKER: /usr/lib/dyld                                         |
| LC_LOAD_DYLIB: /usr/lib/libSystem.B.dylib                               |
| LC_LOAD_DYLIB: /System/Library/Frameworks/CoreFoundation.framework/... |
| LC_DYLD_INFO_ONLY:                                                      |
|   rebase_off / rebase_size (Opcode-encoded base relocations)            |
|   bind_off / bind_size (Opcode-encoded external symbol binding)         |
|   lazy_bind_off / lazy_bind_size (Per-stub on-demand binding)           |
|   export_off / export_size (Prefix trie of exported symbols)           |
| LC_SYMTAB: symoff, nsyms, stroff, strsize                               |
| LC_DYSYMTAB: ilocalsym, iextdefsym, iundefsym, indirectsymoff           |
+-------------------------------------------------------------------------+
```

### Key Load Commands Analyzed

1. **`LC_LOAD_DYLIB` (`0x0000000C`)**:
   Contains dylib path, timestamp, current version, and compatibility version. Identifies libraries that must be loaded prior to binary execution.
2. **`LC_ID_DYLIB` (`0x0000000D`)**:
   Found in `.dylib` files to declare their canonical install name.
3. **`LC_DYLD_INFO_ONLY` (`0x80000022`)**:
   Introduced in OS X 10.6 / iOS 3.1 to replace traditional relocation tables with compact, opcode-compressed streams:
   - **Rebase stream**: adjusts interior pointers when an ASLR slide is applied.
   - **Bind stream**: specifies target library ordinal (`BIND_OPCODE_SET_DYLIB_ORDINAL_IMM`), symbol name string (`BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM`), segment and offset, and commits binding (`BIND_OPCODE_DO_BIND`).
   - **Export trie**: directed acyclic graph encoding exported symbol names, flags, and function offsets.
4. **`LC_DYLD_CHAINED_FIXUPS` (`0x80000034`)**:
   Modern iOS/macOS 12+ format where relocations and bindings are chained in pointer slots across the data segment.

---

## 3. Feasibility Comparison: Full `dyld` vs Bounded Guest Dynamic Loader

| Dimension | Option A: Full Apple `dyld` Port | Option B: Bounded `GuestDynamicLoader` (Recommended) |
|---|---|---|
| **Codebase Scope** | ~200,000+ lines of intricate C++20 | ~800 lines of focused, auditable C++20 |
| **Dependencies** | Requires dyld shared cache, complex Mach VM syscalls, XNU kernel shared regions, libdispatch, libc++ | Zero external dependencies; operates strictly within WinIOS `UserAddressSpace` and `Memory` |
| **Deterministic Validation** | High risk of hidden host assumptions, non-deterministic initialization | 100% deterministic, reproducible across test suites and architectures |
| **Dynamic Symbol Resolution** | Full support (lazy, weak, interposing, dlsym) | Full immediate binding of imports to exported symbols |
| **Shared Library Mapping** | Maps into shared region (`0x180000000`+) | Maps `.dylib` segments into independent process address spaces |
| **Host Portability** | Extremely difficult on Windows 11 host | Native MSVC C++20 compliant (`/W4 /WX /permissive-`) |

---

## 4. Bounded `GuestDynamicLoader` Design

For WinIOS Campaign 219, we implement **Option B**: a deterministic, bounded dynamic loader (`GuestDynamicLoader`):

1. **Inspection**:
   Parses `LC_LOAD_DYLIB` commands to identify all dependent libraries.
2. **Segment Mapping**:
   Allocates collision-free virtual address ranges for each dylib (e.g. `0x180000000`, `0x190000000`).
   Maps `__TEXT` (read-exec) and `__DATA` (read-write) pages via `ppm` and `UserAddressSpace`.
3. **Symbol Table Resolution**:
   Parses the dylib's `LC_SYMTAB` / string table or export trie to construct a symbol name -> target VA map.
4. **Binding & Patching**:
   Locates undefined symbol entries in the executable's `LC_DYSYMTAB` / indirect symbol table (`__got` section) and writes the resolved target virtual address into each pointer slot.
5. **Execution Transfer**:
   Transfers control to the main binary's entry point (`LC_MAIN`), where guest ARM64 code can transparently call functions residing inside the dynamically loaded shared library!
