# WINIOS CAMPAIGN 217 — DEVELOPER BUILD & PACKAGING WORKFLOW

**Tool:** `tools/wpack.py`  
**Toolchain:** Python 3, Zig 0.17.0 (`-target aarch64-macos`), Visual Studio MSVC host  
**Package Extension:** `.wios`

---

## 1. Developer Tooling Overview

`wpack.py` provides a unified CLI for the end-to-end lifecycle of WinIOS guest applications:

```
                  +-----------------------------------+
                  |      Developer C/C++ Source       |
                  +-----------------------------------+
                                    |
                                    v
                           [ wpack compile ]
                         (Zig 0.17.0 toolchain)
                                    |
                                    v
                  +-----------------------------------+
                  |     ARM64 Mach-O Binary Exec      |
                  +-----------------------------------+
                                    |
                                    v
                          [ wpack validate ]
                     (Inspects Mach-O 64-bit headers)
                                    |
                                    v
                             [ wpack pack ]
                   (Bundles binary + manifest + assets)
                                    |
                                    v
                  +-----------------------------------+
                  |         App Package (.wios)       |
                  +-----------------------------------+
                                    |
                                    v
                            [ wpack verify ]
                    (Validates SHA256 & structure)
```

---

## 2. Command Reference

### 2.1 Compile Source to ARM64 Mach-O
```cmd
python tools/wpack.py compile src/apps/my_app.c -o build/my_app.macho
```
Invokes `zig cc -target aarch64-macos -nostdlib -fno-sanitize=undefined -O2 -Wl,-e,__start` to produce freestanding 64-bit Mach-O binaries.

### 2.2 Validate Mach-O Binary
```cmd
python tools/wpack.py validate build/my_app.macho
```
Checks for:
- 64-bit Mach-O magic (`0xFEEDFACF`)
- CPU Type `0x0100000C` (`CPU_TYPE_ARM64`)
- Valid load commands (`LC_SEGMENT_64`, `LC_MAIN`)

### 2.3 Assemble Package
```cmd
python tools/wpack.py pack --manifest app.json --binary build/my_app.macho --assets assets/ -o build/my_app.wios
```
Generates a standard `.wios` package containing the application manifest, ARM64 binary, and optional resource bundle.

### 2.4 Verify Package
```cmd
python tools/wpack.py verify build/my_app.wios
```
Validates the internal package structure, manifest syntax, and computes the SHA-256 package hash.
