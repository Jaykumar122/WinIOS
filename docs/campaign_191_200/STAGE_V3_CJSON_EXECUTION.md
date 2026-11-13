# Campaign 191-V Stage V3 — cJSON Open-Source Application Execution

## 1. Upstream Provenance and Driver Architecture

- **Upstream Source:** Dave Gamble's [`cJSON`](https://github.com/DaveGamble/cJSON) (MIT License).
- **Driver Source:** [`tests/artifacts/src/app_cjson.c`](file:///D:/WinISO26/WinIOS/tests/artifacts/src/app_cjson.c).
- **Workload:** Parses structured JSON with strings, numbers, booleans, arrays, and nested objects:
  ```json
  {"project":"WinIOS","campaign":"191-V","version":191,"active":true,"pi":3.14159,"scores":[10,20,30],"metadata":{"arch":"arm64","target":"ios"}}
  ```
  Traverses the AST, validates integer and floating-point values, validates array items, re-serializes unformatted JSON, and safely frees all dynamically allocated nodes.
- **Expected Output:**
  ```
  JSON_MATCH=1
  ```

## 2. Compilation and Artifact Metadata

All binaries were compiled independently with `Zig 0.17.0` (Clang `22.1.8` frontend and LLVM Mach-O linker). Dynamic linking targets [`minidyld`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/minidyld) (`REAL-017`) and [`libSystem.B.dylib`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/libSystem.B.dylib) (`REAL-018`).

| Artifact ID | Binary Name | Target Platform | Optimization | Fixup Format | Size (bytes) | SHA-256 |
|---|---|---|---|---|---:|---|
| `REAL-028` | `cjson_app_O0` | `PLATFORM_MACOS` (1) | `-O0` | `LC_DYLD_INFO_ONLY` | 97,424 | `e8772ed70eb75995e2788e9e8e796d707f23346c14059b08d327bb4e492f24fb` |
| `REAL-029` | `cjson_app_O2` | `PLATFORM_MACOS` (1) | `-O2` | `LC_DYLD_INFO_ONLY` | 71,264 | `3d0ffba34b94f31ff28a568c2857d16a8cdbc8c4e87af01ba22f178036011a6d` |
| `REAL-030` | `cjson_app_ios` | `PLATFORM_IOS` (2) | `-O2` | `LC_DYLD_CHAINED_FIXUPS` | 71,336 | `f18ce6f30ef1ef86416481e74fc4226c47e86408040e602544bd83b0534ce06b` |

## 3. Dynamic Memory Allocation and Libsystem Integration

cJSON exercises intense dynamic heap allocation:
- Calls `malloc`, `free`, and custom allocation wrappers in [`libSystem.B.dylib`](file:///D:/WinISO26/WinIOS/tests/artifacts/bin/libSystem.B.dylib).
- Tests floating-point formatting and string parsing routines (`strtod`, `fabs`, `sscanf` emulation).
- Uses nested pointers and string duplication (`strdup`, `strlen`, `strcmp`).

## 4. Execution Evidence & Verification

All three variants were executed directly in [`tests/unit/stage_v3_large_opensource_tests.cpp`](file:///D:/WinISO26/WinIOS/tests/unit/stage_v3_large_opensource_tests.cpp) via `minidyld` and `libSystem.B.dylib`.

| Variant | Retired Instructions | Exit Code | Stdout Matches Host Truth | Status |
|---|---:|---:|:---:|:---:|
| `REAL-028` (`cjson_app_O0`) | 83,747 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-029` (`cjson_app_O2`) | 52,310 | 0 | YES (100% bit-for-bit) | PASS |
| `REAL-030` (`cjson_app_ios`) | 49,685 | 0 | YES (100% bit-for-bit) | PASS |

### Ground Truth Differential Verification
- Independent host x64 binary execution:
  `JSON_MATCH=1`
- WinIOS guest ARM64 execution (`REAL-028`, `REAL-029`, `REAL-030`):
  `JSON_MATCH=1`
- **Result:** Complete syntactic and AST semantic match across all 3 variants.
