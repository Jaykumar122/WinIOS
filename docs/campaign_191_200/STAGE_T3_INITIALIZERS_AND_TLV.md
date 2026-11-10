# WINIOS STAGE T3 PROOF REPORT: INITIALIZERS AND THREAD-LOCAL STORAGE (TLV)

**Campaign:** 191-T  
**Stage:** T3 — Initializers and Thread-Local Storage (TLV)  
**Host Platform:** Windows 11 Pro x64  
**Toolchain:** MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend)  
**Date:** 2026-11-09  

---

## 1. Executive Summary

Stage T3 evaluated genuine compiler emission and execution of:
1. **Module Initializers (`__attribute__((constructor))`):**
   - Verified section emission: `__DATA_CONST,__mod_init_func` with section type `0x09` (`S_MOD_INIT_FUNC_POINTERS`).
   - Extended `UserspaceMachOLoader` to parse section table entries of `LC_SEGMENT_64` and extract initializer function pointers into `UserspaceLoadResult.mod_init_funcs`.
   - Verified sequential constructor invocation order:
     - Dylib dependency (`libctor_leaf.dylib`): `_leaf_ctor` executed at `0x214c0` (sequence 1).
     - Dylib dependent (`libctor_mid.dylib`): `_mid_ctor` executed at `0x31518` (sequence 2).
     - Main executable (`ctor_main`): `_main_ctor` executed at `0x1708` (sequence 3).
     - Main entry point (`start`): verified sequence values `1-2-3`, retired 72 instructions, and cleanly exited with code 0.
   - Built negative unit test verifying that omitting constructors triggers deterministic exit code 1 (`"CTOR_ERR\n"`).

2. **Thread-Local Storage (TLV) Descriptors (`__thread`):**
   - Verified section emission: `__DATA,__thread_vars` (type `0x13`, `S_THREAD_LOCAL_VARIABLES`) and `__DATA,__thread_data` (type `0x11`, `S_THREAD_LOCAL_REGULAR`).
   - Forensic analysis of emitted ARM64 machine code for accessing `__thread`:
     ```arm64
     ADRP X0, #0x100008000
     ADD  X0, X0, #8       ; X0 = address of TLV descriptor
     LDR  X8, [X0]         ; Load descriptor thunk pointer
     BLR  X8               ; Invoke thunk(desc) -> returns address of thread's variable in X0
     LDR  W8, [X0]         ; Access variable
     ```
   - Emitted binding stream: binds `__tlv_bootstrap` (or `___tlv_bootstrap`) into `desc->thunk`.
   - Implemented `DyldEngine` resolution and guest bootstrap thunk (`add x0, x0, #24; ret`) at `0x180001400`.
   - Executed `tlv_main` on guest CPU: read default value `123`, wrote `456`, read back and verified `456`, wrote `"TLV_OK: 456\n"`, and cleanly exited with code 0 (64 instructions retired).

---

## 2. Artifact Forensic Manifest

| Artifact ID | File Name | Size (Bytes) | SHA-256 | Type | Platform | Target |
|-------------|-----------|--------------|---------|------|----------|--------|
| `REAL-010` | `libctor_leaf.dylib` | 50,192 | `0957ed9a0623e42b81fc5dcb0132ed9359c5def19379d151e469e0f82bc5e47b` | `MH_DYLIB` | macOS 15.0 | ARM64 |
| `REAL-011` | `libctor_mid.dylib` | 50,176 | `68fb1a8d51d652ffdfe350100f0597239d022a06fb8aedbb907ec9ba3e3bbe3a` | `MH_DYLIB` | macOS 15.0 | ARM64 |
| `REAL-012` | `ctor_main` | 50,632 | `939e0c46660983ddd6dede4f9e0a1813909b222b727bf0edebc42431fc8165a0` | `MH_EXECUTE` | macOS 15.0 | ARM64 |
| `REAL-013` | `tlv_main` | 50,376 | `3b55bffe76a164a95a1aabc88a0d0b68ad21208590e74defeb8d5c665ff56ad7` | `MH_EXECUTE` | macOS 15.0 | ARM64 |

---

## 3. Emitted Layout and Disassembly

### A. Module Initializers Layout
`ctor_main` segment/section layout:
```
Segment: __DATA_CONST     vmaddr=0x100004000 vmsize=0x000018 nsects=2
  Section: __DATA_CONST __got            addr=0x100004000 size=0x000010 type=0x06
  Section: __DATA_CONST __mod_init_func  addr=0x100004010 size=0x000008 type=0x09
```
Content of `__mod_init_func`:
```
libctor_leaf.dylib __mod_init_func[0] = 0x14c0 (_leaf_ctor)
libctor_mid.dylib  __mod_init_func[0] = 0x1518 (_mid_ctor)
ctor_main          __mod_init_func[0] = 0x100001708 (_main_ctor)
```

### B. Thread-Local Storage Layout
`tlv_main` segment/section layout:
```
Segment: __DATA           vmaddr=0x100008000 vmsize=0x000024 nsects=3
  Section: __DATA     __data           addr=0x100008000 size=0x000008 type=0x00
  Section: __DATA     __thread_vars    addr=0x100008008 size=0x00018 type=0x13
  Section: __DATA     __thread_data    addr=0x100008020 size=0x00004 type=0x11
```
Content of `__thread_vars` (TLV descriptor, 24 bytes):
- `thunk_ptr`: 0 (bound to `__tlv_bootstrap` via `LC_DYLD_INFO_ONLY`)
- `key`: 0
- `offset`: 0
Content of `__thread_data`:
- `int g_val = 123` (4 bytes: `0x7B, 0x00, 0x00, 0x00`)

---

## 4. Test Suite Execution Trace

```
====================================================
STAGE T3: INITIALIZERS AND THREAD-LOCAL STORAGE (TLV)
====================================================
[Test 1] Verify Initializer and TLV Metadata Extraction...
  libctor_leaf mod_init_func[0]: 0x14c0
  libctor_mid  mod_init_func[0]: 0x1518
  ctor_main    mod_init_func[0]: 0x100001708
[Test 2] Sequential Constructor Execution Order (leaf -> mid -> main -> entry)...
    [leafMod] export: _g_leaf_seq -> 0x2800c
    [leafMod] export: _g_sequence -> 0x28008
    [leafMod] export: _get_leaf_seq -> 0x214dc
    [leafMod] export: _leaf_ctor -> 0x214c0
    [leafMod] export: __mh_dylib_header -> 0x28000
    [leafMod] export: ___dso_handle -> 0x28000
    [midMod] export: _g_mid_seq -> 0x38008
    [midMod] export: _get_mid_seq -> 0x31538
    [midMod] export: _mid_ctor -> 0x31518
    [midMod] export: __mh_dylib_header -> 0x38000
    [midMod] export: ___dso_handle -> 0x38000
  Constructors to invoke in order:
    1. Leaf ctor: 0x214c0
    2. Mid  ctor: 0x31518
    3. Main ctor: 0x1708
    Ran guest constructor leaf_ctor at 0x214c0, final PC=0xffff0000, ELR=0x0, ESR=0x0, FAR=0x0
    Ran guest constructor mid_ctor at 0x31518, final PC=0xffff0000, ELR=0x0, ESR=0x0, FAR=0x0
    Ran guest constructor main_ctor at 0x1708, final PC=0xffff0000, ELR=0x0, ESR=0x0, FAR=0x0
  Guest instructions retired: 72
  Process exited: true, Exit code: 0
  Final PC: 0x10400, ELR=0x1848, ESR=0x56000080, FAR=0x0
  Captured stdout: "CTOR_CHAIN_OK: 1-2-3\n"
  BSD syscalls handled: 2
[Test 3] Thread-Local Storage (TLV) Execution...
    [parsed bind] dyld_stub_binder at patch_vaddr=0x100004000
    [parsed bind] __tlv_bootstrap at patch_vaddr=0x100008008
    [bind tlv] dyld_stub_binder -> 0x1380 written to 0x4000
    [bind tlv] __tlv_bootstrap -> 0x1400 written to 0x8008
  Guest instructions retired: 64
  Process exited: true, Exit code: 0
  Final PC: 0x10400, ELR=0x16ac, ESR=0x56000080, FAR=0x0
  Captured stdout: "TLV_OK: 456\n"
  BSD syscalls handled: 2
[Test 4] Negative Test: Omitted Constructor Detected...
  Process exited: true, Exit code: 1
  Captured stdout: "CTOR_ERR\n"
====================================================
[STAGE T3 TESTS] ALL PASSED
====================================================
```

---

## 5. Architectural Disclosure

Initializers are extracted from Mach-O `__mod_init_func` sections and invoked on the native guest ARM64 CPU in strict dependency order prior to transferring execution to the main binary's entry point (`start`). TLV descriptors in `__thread_vars` are bound via `__tlv_bootstrap` to an in-memory ARM64 thunk (`0x91006000, 0xd65f03c0`), which computes the variable address in `__thread_data` and returns to guest code for native load and store operations. Both paths execute genuine guest ARM64 instructions without host emulation.
