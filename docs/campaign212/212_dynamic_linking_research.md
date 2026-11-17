# WINIOS CAMPAIGN 212 — STAGE 212.6 MACH-O DYNAMIC LINKING RESEARCH

**Campaign:** 212  
**Baseline Commit:** `9ee8978b54e743945a0b6b547cd1ff5ae2bdc315`  
**Host Toolchain:** Windows 11 x64, Visual Studio 2026 MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0  
**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`)  
**Pinned Reference:** `xnu-10002.1.13` & Apple Public dyld Interfaces

---

## 1. Executive Summary

This research document analyzes the Mach-O dynamic linking architecture, load commands, symbol tables, relocation mechanisms, and dependency resolution algorithms required to enable genuine multi-binary ARM64 Mach-O execution in WinIOS. The objective is to design a bounded, reproducible in-engine dynamic loader (`GuestDynamicLoader`) and dependency graph manager (`MachoDependencyGraph`) without depending on Apple's proprietary shared cache or full `dyld`.

---

## 2. Mach-O Dynamic Load Commands Analysis

### 2.1 `LC_LOAD_DYLIB` (`0x0000000C`) & `dylib_command`
- **Definition:** Standard command declaring a dynamic library dependency.
- **Header Layout:**
  ```c
  struct dylib_command {
      uint32_t cmd;                  // LC_LOAD_DYLIB (0xC)
      uint32_t cmdsize;              // includes pathname string
      struct dylib {
          union lc_str name;         // library's path name offset
          uint32_t timestamp;        // library's build timestamp
          uint32_t current_version;  // library's current version number
          uint32_t compatibility_version; // library's compatibility version
      } dylib;
  };
  ```
- **Validation Rules:**
  - `cmdsize >= sizeof(dylib_command)` (24 bytes).
  - String offset `dylib.name.offset < cmdsize`.
  - String must be null-terminated within the command buffer.
  - Path length clamped to max 1024 characters.

### 2.2 `LC_ID_DYLIB` (`0x0000000D`)
- **Definition:** Declares the install name (soname) of a shared library (`MH_DYLIB`).
- **Use Case:** Used by `GuestDynamicLoader` to identify and index loaded libraries, preventing duplicate image mappings.

### 2.3 `LC_LOAD_WEAK_DYLIB` (`0x80000018`) & `LC_REEXPORT_DYLIB` (`0x8000001F`)
- **`LC_LOAD_WEAK_DYLIB`:** Contains `LC_REQ_DYLD` bit (`0x80000000`). If the target dylib cannot be found at runtime, the loader does not abort; unresolved symbols remain null.
- **`LC_REEXPORT_DYLIB`:** Re-exports all symbols from the child dylib as part of the parent dylib's public interface.

### 2.4 `LC_RPATH` (`0x8000001C`)
- **Definition:** Runpath search path.
- **Structure:** `struct rpath_command` with `union lc_str path`.
- **Resolution:** Replaces `@rpath` prefix in dependency paths (e.g. `@rpath/libwinios_runtime.dylib`) with explicit search directories (e.g., `/usr/lib`, `/lib`, or guest virtual bundles).

### 2.5 `LC_SYMTAB` (`0x00000002`) & `LC_DYSYMTAB` (`0x0000000B`)
- **Classical Symbol Table (`nlist_64`):**
  ```c
  struct nlist_64 {
      union {
          uint32_t n_strx;   // index into string table
      } n_un;
      uint8_t n_type;        // type flag (N_EXT, N_UNDF, N_SECT)
      uint8_t n_sect;        // section number (1-based)
      uint16_t n_desc;       // description / symbol flags
      uint64_t n_value;      // symbol virtual address
  };
  ```
- **Dynamic Symbol Table (`dysymtab_command`):**
  - Indexes local symbols (`ilocalsym`, `nlocalsym`).
  - Indexes externally defined symbols (`iextdefsym`, `nextdefsym`).
  - Indexes undefined imported symbols (`iundefsym`, `nundefsym`).
  - Indexes indirect symbol table (`indirectsymoff`, `nindirectsyms`) for lazy and non-lazy pointer tables.

### 2.6 `LC_DYLD_INFO_ONLY` (`0x80000022`) & Export Trie
- **Rebase Info:** Bytecode stream updating interior pointers when an ASLR slide is applied.
- **Bind Info:** Bytecode stream resolving external symbol references.
- **Export Trie:** Prefix tree mapping exported symbol names to segment offsets and flags.

---

## 3. ARM64 Relocation Types

| Relocation Type | Value | Semantics in Darwin ARM64 | Bounded WinIOS Support |
|:---|:---:|:---|:---|
| `ARM64_RELOC_UNSIGNED` | 0 | 64-bit absolute address / pointer rebase | **Supported** |
| `ARM64_RELOC_SUBTRACTOR` | 1 | Difference between two symbol addresses | **Supported** (Static/Object) |
| `ARM64_RELOC_BRANCH26` | 2 | 26-bit PC-relative B/BL target | Handled by dynamic stub |
| `ARM64_RELOC_PAGE21` | 3 | ADRP 21-bit page base offset | Handled by compiler/stub |
| `ARM64_RELOC_PAGEOFF12` | 4 | 12-bit page offset for LDR/ADD | Handled by compiler/stub |
| `ARM64_RELOC_GOT_LOAD_PAGE21` | 5 | ADRP for GOT entry | **Supported** via GOT patching |
| `ARM64_RELOC_GOT_LOAD_PAGEOFF12` | 6 | LDR offset for GOT entry | **Supported** via GOT patching |
| `ARM64_RELOC_POINTER_TO_GOT` | 7 | Pointer to GOT slot | **Supported** via GOT patching |

---

## 4. Supported vs Unsupported Capabilities Matrix

| Feature | Status | Notes |
|:---|:---:|:---|
| `LC_LOAD_DYLIB` Parsing | **Supported** | Validates lengths, paths, versions; enforces max count (16). |
| `LC_ID_DYLIB` Extraction | **Supported** | Extracts dylib identity name; prevents duplicate mappings. |
| `LC_RPATH` Expansion | **Supported** | Substitutes `@rpath` with configured runtime paths. |
| Cycle Detection | **Supported** | Tarjan/DFS cycle detection in dependency graph. |
| Depth Limiting | **Supported** | Enforces max recursion depth of 8 levels. |
| Symbol Binding via `LC_SYMTAB` | **Supported** | Binds undefined symbols in executable to exported dylib symbols. |
| Pointer Rebasing | **Supported** | Applies slide offset to `__DATA` pointer entries. |
| GOT / Non-Lazy Pointer Patching | **Supported** | Writes resolved symbol VAs into `__DATA,__got` and `__la_symbol_ptr`. |
| Apple Shared Cache Format | *Unsupported* | Out of scope; WinIOS loads discrete `.dylib` images directly. |
| Chained Fixups (`LC_DYLD_CHAINED_FIXUPS`) | *Unsupported* | Classical `LC_SYMTAB`/`LC_DYSYMTAB` binding used for test images. |
| Two-Level Hint Tables | *Unsupported* | Linear lookup over loaded dependencies. |
