# WINIOS STAGE T2 PROOF REPORT: THREE-IMAGE DEPENDENCY CHAIN WITH @rpath

**Campaign:** 191-T  
**Stage:** T2 — Three-Image Dependency Chain with `@rpath`  
**Host Platform:** Windows 11 Pro x64  
**Toolchain:** MSVC C++20 (`/W4 /WX /permissive-`), Zig 0.17.0 (Clang 22.1.8 + lld/Mach-O backend)  
**Date:** 2026-11-09  

---

## 1. Executive Summary

Stage T2 required building and executing a freestanding three-image dependency chain:
1. `libleaf.dylib` (exports `_leaf_mul`, install name `@rpath/libleaf.dylib`)
2. `libmid.dylib` (exports `_mid_compute`, imports `_leaf_mul`, install name `@rpath/libmid.dylib`, `LC_RPATH` `@loader_path`)
3. `main_chain` (entry point `_start`, imports `_mid_compute`, `LC_RPATH` `@executable_path`)

All three artifacts were compiled from source using Zig 0.17.0 (`-target aarch64-macos`). `DyldEngine` was extended with `resolve_rpath_path` to resolve `@rpath` tokens against `@executable_path` and `@loader_path`.

The full chain was admitted, loaded into guest memory, bound, and executed on the WinIOS guest ARM64 CPU.
- **Guest instructions retired:** 66
- **Syscalls handled:** 2 (`SYS_write` fd 1, `SYS_exit` code 0)
- **Captured stdout:** `"CHAIN_OK: 142\n"`
- **Arithmetic verification:** `6 * 7 = 42`, `100 + 42 = 142`.
- **Unit test suite:** `tests/unit/stage_t2_three_image_chain_tests.cpp` (4/4 tests passed).

---

## 2. Artifact Provenance and Forensic Manifest

| Artifact ID | File Name | Size (Bytes) | SHA-256 | Type | Platform | Target |
|-------------|-----------|--------------|---------|------|----------|--------|
| `REAL-007` | `libleaf.dylib` | 49,952 | `9dcf41e11ad65d815a42b2e9bda2a351750a0cc38d7f04f6b5461664e33a2a2d` | `MH_DYLIB` | macOS 15.0 | ARM64 |
| `REAL-008` | `libmid.dylib` | 50,048 | `218803ae3ade56294bddf187da391fa455b3d103b73437a58b2183630511afbb` | `MH_DYLIB` | macOS 15.0 | ARM64 |
| `REAL-009` | `main_chain` | 50,232 | `f8aba05b9396ded14b8cd235b5c4168f34155cc6c57fd3b42072cccecb96e6ed` | `MH_EXECUTE` | macOS 15.0 | ARM64 |

### Build Commands:
- `libleaf.dylib`:
  ```sh
  zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-stack-protector -install_name @rpath/libleaf.dylib tests/artifacts/src/libleaf.c -o tests/artifacts/bin/libleaf.dylib
  ```
- `libmid.dylib`:
  ```sh
  zig cc -target aarch64-macos -dynamiclib -nostdlib -fno-stack-protector -install_name @rpath/libmid.dylib -rpath @loader_path tests/artifacts/src/libmid.c tests/artifacts/bin/libleaf.dylib -o tests/artifacts/bin/libmid.dylib
  ```
- `main_chain`:
  ```sh
  zig cc -target aarch64-macos -nostdlib -fno-stack-protector -rpath @executable_path -Wl,-e,_start tests/artifacts/src/main_chain.c tests/artifacts/bin/libmid.dylib -o tests/artifacts/bin/main_chain
  ```

---

## 3. Dynamic Rpath Resolution Architecture

Darwin binaries resolve dylibs using `@rpath` substitution against `LC_RPATH` commands present in the calling binary or loaded chain.
`DyldEngine::resolve_rpath_path` implements this resolution:
1. When a dylib path starts with `@rpath/`:
2. For each search path in `rpaths`:
   - `@executable_path` is replaced with the directory containing the main executable.
   - `@loader_path` is replaced with the directory containing the loading image.
   - Normal path candidates are checked on host filesystem.
3. Returns the concrete filesystem path of the resolved dylib.

### Cross-Binary Verification:
- `main_chain` requires `@rpath/libmid.dylib`. `main_chain` specifies `LC_RPATH = @executable_path`.
  - Resolved: `WinIOS/tests/artifacts/bin/libmid.dylib`.
- `libmid.dylib` requires `@rpath/libleaf.dylib`. `libmid.dylib` specifies `LC_RPATH = @loader_path`.
  - Resolved: `WinIOS/tests/artifacts/bin/libleaf.dylib`.

---

## 4. Execution Trace and Proof

```
====================================================
STAGE T2: THREE-IMAGE DEPENDENCY CHAIN WITH @rpath
====================================================
[Test 1] Three-Image Metadata, Rpaths and Transitive Resolution...
  Resolved libmid:  WinIOS/tests/artifacts/bin/libmid.dylib
  Resolved libleaf: WinIOS/tests/artifacts/bin/libleaf.dylib
[Test 2] Execute Three-Image Transitive Chain (main_chain -> libmid -> libleaf)...
  leaf export off=0xc018 sz=48
  mid export off=0xc030 sz=48 bind off=0xc008 sz=18 lazy off=0xc020 sz=10
  main bind off=0xc008 sz=18 lazy off=0xc020 sz=18
    [leafMod] export: _leaf_mul -> 0x21468
    [leafMod] export: __mh_dylib_header -> 0x28000
    [leafMod] export: ___dso_handle -> 0x28000
    [midMod] export: _mid_compute -> 0x315a8
    [midMod] export: __mh_dylib_header -> 0x38008
    [midMod] export: ___dso_handle -> 0x38008
    [bind mid] dyld_stub_binder -> 0x180001380 written to 0x34000
    [bind mid] _leaf_mul -> 0x21468 written to 0x38000
    [bind main] dyld_stub_binder -> 0x180001380 written to 0x4000
    [bind main] _mid_compute -> 0x315a8 written to 0x8000
  Guest instructions retired: 66
  Process exited: true, Exit code: 0
  Captured stdout: "CHAIN_OK: 142\n"
  BSD syscalls handled: 2
  Final PC: 0x10400
  ELR_EL1: 0x16dc, ESR_EL1: 0x56000080, FAR_EL1: 0x0
[Test 3] Negative Test: Missing Leaf Dylib Resolution...
[Test 4] Negative Test: Bad Rpath and Export Trie Corruption...
====================================================
[STAGE T2 TESTS] ALL PASSED
```

---

## 5. Architectural Disclosure

In accordance with strict campaign verification standards:
Dynamic library binding and symbol resolution are performed **HOST-EMULATED** via `pre_execution_hook` prior to entering guest CPU instruction stepping. Once GOT slots are bound, execution transitions to native ARM64 guest CPU instruction retirement across all three image text segments (`main_chain` at `0x1000016b0`, `libmid` at `0x315a8`, `libleaf` at `0x21468`). No host CPU trampoline is involved in calling `_mid_compute` or `_leaf_mul`.
