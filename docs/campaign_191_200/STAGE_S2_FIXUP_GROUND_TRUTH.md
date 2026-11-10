# STAGE S2: FIXUP & EXPORT TRIE GROUND TRUTH AUDIT

**Campaign:** WINIOS CAMPAIGN 191-S  
**Stage:** S2 — First Real Dylib: Fixup & Export Formats  
**Target:** ARM64 Darwin Userspace Dynamic Linking  
**Date:** 2026-11-09  

---

## 1. EXECUTIVE SUMMARY & ARTIFACTS INSPECTED

During Stage S2, two genuine independently-compiled Mach-O artifacts were generated using Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend):
1. `libmathreal.dylib` (`REAL-004`): Dynamic shared library exporting `_real_add`, `_real_read_global`, and `_g_real_magic`.
2. `main_with_dylib` (`REAL-005`): Executable binary dynamically linked to `/usr/lib/libmathreal.dylib`.

Prior WinIOS campaigns assumed that modern compiler targets on macOS/Darwin invariably emit `LC_DYLD_CHAINED_FIXUPS` and `LC_DYLD_EXPORTS_TRIE`. Independent binary inspection of the toolchain's emitted binaries revealed critical ground truth about real fixup formats.

---

## 2. GROUND TRUTH LOAD COMMAND FORENSICS

### A. Dynamic Library: `tests/artifacts/bin/libmathreal.dylib`
- **File Type:** `MH_DYLIB` (`filetype = 6`)
- **Commands Count:** 16 load commands
- **Key Load Commands:**
  - `[0] LC_SEGMENT_64 (__TEXT)`: `vmaddr = 0x0`, `vmsize = 0x1528`
  - `[1] LC_SEGMENT_64 (__DATA_CONST)`: `vmaddr = 0x4000`, `vmsize = 0x8`
  - `[2] LC_SEGMENT_64 (__DATA)`: `vmaddr = 0x8000`, `vmsize = 0x10` (stores `g_real_magic`)
  - `[4] LC_DYLD_INFO_ONLY`:
    - `rebase`: `off = 0xc000`, `size = 0`
    - `bind`:   `off = 0xc000`, `size = 24`
    - `weak`:   `off = 0xc018`, `size = 0`
    - `lazy`:   `off = 0xc018`, `size = 0`
    - `export`: `off = 0xc018`, `size = 112`
  - `[10] LC_ID_DYLIB`: `"/usr/lib/libmathreal.dylib"`
  - `[14] LC_LOAD_DYLIB`: `"/usr/lib/libSystem.B.dylib"`

### B. Linked Executable: `tests/artifacts/bin/main_with_dylib`
- **File Type:** `MH_EXECUTE` (`filetype = 2`)
- **Commands Count:** 18 load commands
- **Key Load Commands:**
  - `[0] LC_SEGMENT_64 (__PAGEZERO)`: `vmaddr = 0x0`, `vmsize = 0x100000000`
  - `[1] LC_SEGMENT_64 (__TEXT)`: `vmaddr = 0x100000000`, `vmsize = 0x1a08`
  - `[2] LC_SEGMENT_64 (__DATA_CONST)`: `vmaddr = 0x100004000`, `vmsize = 0x10`
  - `[3] LC_SEGMENT_64 (__DATA)`: `vmaddr = 0x100008000`, `vmsize = 0x20`
  - `[5] LC_DYLD_INFO_ONLY`:
    - `rebase`: `off = 0xc000`, `size = 8`
    - `bind`:   `off = 0xc008`, `size = 48`
    - `weak`:   `off = 0xc038`, `size = 0`
    - `lazy`:   `off = 0xc038`, `size = 64`
    - `export`: `off = 0xc078`, `size = 88`
  - `[11] LC_MAIN`: `entryoff = 0x1854`, `stacksize = 0x1000000`
  - `[15] LC_LOAD_DYLIB`: `"/usr/lib/libSystem.B.dylib"`
  - `[16] LC_LOAD_DYLIB`: `"/usr/lib/libmathreal.dylib"` (ordinal 2)

---

## 3. FIXUP FORMAT COMPARISON & GAP ANALYSIS

| Feature | Prior WinIOS Assumption (Campaign 171–190) | Reality in Genuine Toolchain Output |
| :--- | :--- | :--- |
| **Fixup Command** | Assumed `LC_DYLD_CHAINED_FIXUPS` (`0x80000034`) | Uses standard `LC_DYLD_INFO_ONLY` (`0x80000022`) |
| **Fixup Representation** | Chained pointer starts per page | Bytecode opcode streams (`BIND_OPCODE_*`) |
| **Export Representation** | Assumed `LC_DYLD_EXPORTS_TRIE` (`0x80000033`) | Export trie embedded inside `LC_DYLD_INFO_ONLY` (`export_off`/`export_size`) |
| **Binding Mechanism** | Direct memory array patching | Opcode state machine modifying segment offsets |

### Ground Truth Conclusion:
The toolchain emitted classic bytecode-driven dyld opcodes, which are the fundamental ABI of Darwin dynamic linking across macOS and iOS. WinIOS previously had tests for manual `DyldBindingRecord` structs and synthetic chained fixups, but lacked an automated parser for raw `LC_DYLD_INFO_ONLY` binding opcode streams.

---

## 4. ARCHITECTURAL REMEDIATION: DYLD OPCODE STREAM PARSER

To bridge this gap, `DyldEngine::parse_binding_stream` was implemented in `src/loader/dyld_engine.cpp`:

### Supported Opcodes:
- `0x00`: `BIND_OPCODE_DONE` (stream terminator / stub delimiter)
- `0x10`: `BIND_OPCODE_SET_DYLIB_ORDINAL_IMM`
- `0x20`: `BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB`
- `0x30`: `BIND_OPCODE_SET_DYLIB_SPECIAL_IMM`
- `0x40`: `BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM` (extracts symbol name & weak flag)
- `0x50`: `BIND_OPCODE_SET_TYPE_IMM` (pointer type)
- `0x60`: `BIND_OPCODE_SET_ADDEND_SLEB`
- `0x70`: `BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB` (selects target segment and offset)
- `0x80`: `BIND_OPCODE_ADD_ADDR_ULEB`
- `0x90`: `BIND_OPCODE_DO_BIND` (emits binding record and advances by pointer size)
- `0xA0`: `BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB`
- `0xB0`: `BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED`
- `0xC0`: `BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB`

### Verified Extraction from `main_with_dylib`:
1. `___stack_chk_fail` -> `patch_vaddr = 0x100008000`, ordinal 1
2. `_real_add` -> `patch_vaddr = 0x100008008`, ordinal 2
3. `_real_read_global` -> `patch_vaddr = 0x100008010`, ordinal 2

Every binding was resolved and patched into memory, matching the expectations of the ARM64 stub helpers.
