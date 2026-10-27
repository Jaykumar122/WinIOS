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

## 2026-10-28 — Guest memory, CPU accessors, and automated tests

This is the requested "Phase 4" milestone; in the roadmap it is Phase 5. The
baseline demo behavior is preserved.

### Work completed

- Added `src/memory/memory.h` and `src/memory/memory.cpp`:
  - `std::vector<std::uint8_t>`-backed guest RAM, zero-initialized.
  - Constructor rejects a size of zero with `std::invalid_argument`.
  - Checked `read_u8` / `write_u8`, `read_u32` / `write_u32`, and
    `read_u64` / `write_u64`.
  - Explicit little-endian composition for multi-byte accesses.
  - `require_range()` validates `offset <= size` first, then
    `width <= size - offset`, so oversized offsets cannot overflow the check.
  - Out-of-range and boundary-crossing accesses throw `std::out_of_range`.
- Updated `src/cpu/cpu.h` and `src/cpu/cpu.cpp`:
  - `reset()` clears all 31 registers and the program counter.
  - Checked `read_register()` / `write_register()` for indices 0..30, with
    `std::out_of_range` for 31 and above.
  - `get_program_counter()` / `set_program_counter()`.
  - `run_demo()` retained, still documented as a demonstration and not an
    instruction interpreter.
- Added `tests/unit/test_harness.h`, `tests/unit/memory_tests.cpp`, and
  `tests/unit/cpu_tests.cpp`. The harness counts checks, prints
  `file:line: FAILED: expression`, and returns a nonzero exit code on failure.
  It does not use `assert()`.
- Reworked `CMakeLists.txt`:
  - `winios_core` static library holds `cpu.cpp` and `memory.cpp`.
  - `win_ios` links the core library.
  - `include(CTest)` plus `BUILD_TESTING` guards two test executables.
  - `memory_tests` and `cpu_tests` are registered with `add_test()` and a
    30-second timeout each.

### Configure

Command:

```powershell
cmake -S WinIOS -B WinIOS\build -G "Visual Studio 18 2026" -A x64
```

Actual output (existing build directory reused; same generator):

```text
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.26300.
-- Configuring done (0.3s)
-- Generating done (0.2s)
-- Build files have been written to: D:/WinISO26/WinIOS/build
```

### Build

Command:

```powershell
cmake --build WinIOS\build --config Debug
```

Actual output:

```text
MSBuild version 18.10.1-1.26427.6+3cd27c13e for .NET Framework

  1>Checking Build System
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
  cpu.cpp
  memory.cpp
  Generating Code...
  winios_core.vcxproj -> D:\WinISO26\WinIOS\build\Debug\winios_core.lib
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
  cpu_tests.cpp
  cpu_tests.vcxproj -> D:\WinISO26\WinIOS\build\Debug\cpu_tests.exe
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
  memory_tests.cpp
  memory_tests.vcxproj -> D:\WinISO26\WinIOS\build\Debug\memory_tests.exe
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
  main.cpp
  win_ios.vcxproj -> D:\WinISO26\WinIOS\build\Debug\win_ios.exe
  Building Custom Rule D:/WinISO26/WinIOS/CMakeLists.txt
```

Warnings: none. `/W4 /permissive-` produced no diagnostics.

### Tests

Command:

```powershell
ctest --test-dir WinIOS\build -C Debug --output-on-failure
```

Actual output:

```text
Test project D:/WinISO26/WinIOS/build
    Start 1: memory_tests
1/2 Test #1: memory_tests .....................   Passed    0.06 sec
    Start 2: cpu_tests
2/2 Test #2: cpu_tests ........................   Passed    0.05 sec

100% tests passed out of 2

Total Test time (real) =   0.18 sec
```

CTest exit code: `0`.

Direct runs:

```text
memory_tests: all 53 checks passed
MEMORY_EXIT=0
Demo calculation: X2 = 30
CPU state demonstration complete.
cpu_tests: all 81 checks passed
CPU_EXIT=0
```

(The demo lines appear because `cpu_tests` also checks that `run_demo()`
leaves `X2 = 30` and a zero program counter.)

### Application run

Command:

```powershell
.\WinIOS\build\Debug\win_ios.exe
```

Actual output:

```text
WinIOS v0.1.0
ARM64 emulation research prototype
Demo calculation: X2 = 30
CPU state demonstration complete.
```

Exit code: `0`.

### Coverage of the requested cases

Memory: zero initialization, byte/32-bit/64-bit round trips, little-endian
layout, final valid byte, out-of-bounds byte read/write, huge offsets,
32-bit and 64-bit accesses crossing the boundary, zero-sized construction.

CPU: zeroed construction, reset of all 31 registers, reset of the program
counter, register read/write, index 30 valid, index 31 and huge indices
rejected, program-counter getter/setter, preserved demo result.

### Remaining limitations

- `CPU` and `Memory` are still independent; there is no memory bus, and the
  program counter is not used to fetch anything.
- No instructions are decoded or executed. `run_demo()` uses host arithmetic.
- Alignment, memory permissions, and address translation are not modeled.
- The test harness is project-local and deliberately minimal; it supports
  boolean checks and expected exceptions only.
- No device, firmware, or iOS functionality exists.

### Next milestone

Phase 6: decode and execute a small documented AArch64 subset (`MOVZ`, `ADD`,
`B`) over a 32-bit instruction word, with known-good encodings, an execution
step limit, explicit unsupported-instruction errors, and unit tests for
register results and program-counter updates.
