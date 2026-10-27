# WinIOS Progress Log

This file records what was actually executed and observed. It is not a plan,
and it does not claim results that were not reproduced on this machine.

## 2026-10-28 — Phase 1–4: Baseline skeleton

### Environment (observed)

| Tool | Version / path |
|---|---|
| OS | Windows 11 |
| CMake | 4.4.4 |
| Git | 2.53.0.windows.2 |
| Compiler | MSVC 19.51.36260.0 (Visual Studio Community 2026, 18.10.2) |
| MSVC tools | `C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231` |
| Windows SDK | 10.0.26100.0 |
| Fallback compiler | MinGW.org GCC 6.3.0 at `C:\MinGW\bin\g++.exe` |

The GCC on `PATH` is MinGW.org 6.3.0, which does not support C++20, so the
MSVC toolchain was used. It is the compiler declared in the roadmap.

### Configure

Command:

```powershell
cmake -S WinIOS -B WinIOS\build -G "Visual Studio 18 2026" -A x64
```

Actual output:

```text
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.26300.
-- The CXX compiler identification is MSVC 19.51.36260.0
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Configuring done (4.6s)
-- Generating done (0.0s)
-- Build files have been written to: D:/WinISO26/WinIOS/build
```

### Build

Command:

```powershell
cmake --build WinIOS\build --config Debug
```

Actual output:

```text
MSBuild version 18.10.1-1.26427.6+3cd27c13b for .NET Framework

  1>Checking Build System
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
  main.cpp
  cpu.cpp
  Generating Code...
  win_ios.vcxproj -> D:\WinISO26\WinIOS\build\Debug\win_ios.exe
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
```

Result: build succeeded with no warnings under `/W4 /permissive-`.

### Run

Command:

```powershell
.\build\Debug\win_ios.exe
```

Actual output:

```text
WinIOS v0.1.0
ARM64 emulation research prototype
Demo calculation: X2 = 30
CPU state demonstration complete.
```

Exit code: `0`.

### Phase 4 acceptance

- [x] Project folders created.
- [x] CMake, Git, and a C++20 compiler verified.
- [x] `CMakeLists.txt` created.
- [x] `src/main.cpp`, `src/cpu/cpu.h`, `src/cpu/cpu.cpp` created.
- [x] Project builds and runs.
- [x] Program prints `X2 = 30`.
- [x] Baseline committed to Git.
- [x] Actual build output recorded in this file.

### Known limitations

- `CPU::run_demo()` performs ordinary C++ arithmetic on `registers_`; it is
  not an ARM64 instruction interpreter. No 32-bit instruction words are
  decoded, and no guest program is loaded.
- `program_counter_` is set to 0 and otherwise unused.
- No memory system, loader, debugger, or tests exist yet.
- Genuine iOS 26 boot is unproven and remains an open research question.

### Next phase

Phase 5 — CPU and memory implementation: 64-bit register file with reset
semantics, bounds-checked guest memory, and unit tests for valid and invalid
access. Phase 5 has not been started.
