# STAGE S2: FIRST REAL DYLIB CROSS-BINARY EXECUTION PROOF

**Campaign:** WINIOS CAMPAIGN 191-S  
**Stage:** S2 — First Real Dylib Execution  
**Host:** Windows 11 Pro x64  
**Toolchain:** Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend)  
**Target:** ARM64 Darwin Userspace  
**Date:** 2026-11-09  

---

## 1. OBJECTIVE & SUMMARY

Stage S2 verifies end-to-end multi-image execution on the WinIOS guest CPU:
A genuine freestanding main executable (`main_with_dylib`, `REAL-005`) linked dynamically against a freestanding shared library (`libmathreal.dylib`, `REAL-004`).

**Architecture Note — Host-Emulated Dynamic Binding:**
Dynamic library binding is currently **HOST-EMULATED**. In this architecture, `DyldEngine::parse_binding_stream` runs on the Windows host and `DarwinPipelineConfig.pre_execution_hook` writes resolved dylib target function pointers into the guest GOT before guest EL0 CPU execution commences. There is no guest `/usr/lib/dyld` binary executing at EL0. Once symbols are bound into the guest GOT by the host loader, all subsequent function calls, cross-binary jumps (`BR X16`), and cross-image memory accesses (`g_real_magic`) are executed natively by the guest ARM64 CPU.

Execution was achieved through genuine guest CPU instruction execution:
- Stub helpers loaded target function addresses from dynamically bound GOT slots.
- Jumps were executed via `BR X16` across binary boundaries.
- Cross-binary state was read directly from dylib global memory (`g_real_magic`).
- The entire program completed cleanly with exit code 0 and exact expected stdout output.

---

## 2. ARTIFACT PROVENANCE

| Artifact ID | File Name | Size | SHA-256 Hash | Type |
| :--- | :--- | :--- | :--- | :--- |
| `REAL-004` | `libmathreal.dylib` | 50,144 B | `5d6be8ac5379b472c955350591f839903a700bccc8cd6bdd185ac06f8ead8331` | `MH_DYLIB` |
| `REAL-005` | `main_with_dylib` | 50,784 B | `f4ea6b73091a142636200fdf999f665f06ff8c0db4b3618d76b2b8af94a2cc44` | `MH_EXECUTE` |

### Compiler & Linker Invocations:
```powershell
# REAL-004
zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-sanitize=undefined -install_name /usr/lib/libmathreal.dylib tests/artifacts/src/libmathreal.c -o tests/artifacts/bin/libmathreal.dylib

# REAL-005
zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -Wl,-e,_start -L tests/artifacts/bin -lmathreal tests/artifacts/src/main_with_dylib.c -o tests/artifacts/bin/main_with_dylib
```

---

## 3. STEP-BY-STEP DYNAMIC RESOLUTION & EXECUTION LIFECYCLE

### Step 1: Mapping & Slide Determination
- `libmathreal.dylib` was loaded into guest memory with base address `0x20000` (within 64MB RAM bounds).
- `main_with_dylib` was mapped by `DarwinUserspaceEngine` at standard Darwin virtual addresses (`__TEXT` at 0x0, `__DATA_CONST` at 0x4000, `__DATA` at 0x8000).

### Step 2: Export Trie Symbol Extraction
`DyldEngine::parse_export_trie` parsed the 112-byte export trie in `libmathreal.dylib`:
- `_real_add`: address `0x20000 + 0x1470 = 0x21470`
- `_real_read_global`: address `0x20000 + 0x1490 = 0x21490`
- `_g_real_magic`: address `0x20000 + 0x8008 = 0x28008`

### Step 3: Binding Stream Processing
`DyldEngine::parse_binding_stream` parsed lazy binding opcodes in `main_with_dylib`:
- `_real_add` bound to GOT slot `0x100008008` (mapped to `0x8008`)
- `_real_read_global` bound to GOT slot `0x100008010` (mapped to `0x8010`)

### Step 4: Hook Invocation & Memory Patching
`DarwinPipelineConfig.pre_execution_hook` invoked `dyld.apply_bindings`, writing target function pointers directly into GOT slots before guest CPU instruction retirement commenced.

### Step 5: Guest CPU Execution Trace
1. CPU entered at EL0: `PC = 0x1854` (`_start`), calling `_dylib_main`.
2. `_dylib_main` executed `BL 0x1890` (`__stubs::__stub_real_add`).
3. Stub loaded pointer `0x21470` from `0x8008` and branched via `BR X16`.
4. Guest CPU entered `real_add` inside `libmathreal.dylib` at `0x21470`.
5. `real_add` computed `40 + 2 = 42` in `W0` and executed `RET`.
6. Control returned to `_dylib_main`.
7. `_dylib_main` executed `BL 0x189c` (`__stubs::__stub_real_read_global`).
8. Stub loaded pointer `0x21490` from `0x8010` and branched via `BR X16`.
9. Guest CPU entered `real_read_global` at `0x21490`.
10. `real_read_global` loaded `0xA11C001CAFEBABE` from global variable `g_real_magic` (`0x28008`) via PC-relative `ADRP` + `LDR` and executed `RET`.
11. `_dylib_main` computed combined token: `0xA11C001CAFEBABE + 42 = 0xA11C001CAFEBAE8`.
12. Formatted and emitted output string: `"DYLIB_OK: 0x0A11C001CAFEBAE8\n"` via `SYS_write`.
13. Exited cleanly via `SYS_exit(0)`.

---

## 4. VERIFICATION EVIDENCE

- **Guest Instructions Retired:** 494
- **Process Status:** Terminated normally via `SYS_exit`
- **Exit Code:** 0
- **Captured Output:** `"DYLIB_OK: 0x0A11C001CAFEBAE8\n"`
- **BSD Syscalls Handled:** 4

### Test Suite (`tests/unit/stage_s2_real_dylib_execution_tests.cpp`):
1. `Dylib Artifacts Disk Load & Provenance`: PASSED
2. `Parse Real Mach-O Export Trie (libmathreal.dylib)`: PASSED
3. `Parse Real Binding Stream (main_with_dylib)`: PASSED
4. `Real Dylib Cross-Binary Execution`: PASSED
5. `Negative Test: Missing Dylib Resolution`: PASSED
6. `Negative Test: Undefined Symbol Rejection`: PASSED

**Verdict:** `REAL-004` and `REAL-005` are upgraded to `REAL-ARTIFACT-EXECUTED`.
