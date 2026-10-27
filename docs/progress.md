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

## 2026-10-28 — Phase 5: initial AArch64 instruction interpreter

### Work completed

- Connected `CPU::step(Memory&)` to the existing bounds-checked,
  little-endian `Memory` implementation. It fetches exactly one 32-bit word
  at the current PC; fetch errors are propagated.
- Added explicit reset state for the 64-bit PC and stack pointer. The initial
  and reset PC is zero, ordinary instructions advance PC by four, and
  branches set PC relative to the branch instruction address.
- Implemented and tested only these instruction forms:
  - 64-bit `MOVZ` with immediate and halfword shift. Destination register 31
    is the zero register (`XZR`) and is discarded; unsupported 32-bit `MOVZ`
    is rejected.
  - 64-bit, non-flag-setting `ADD` immediate with optional shift and the
    instruction-specific register-31 `SP` behavior.
  - `B` with signed 26-bit, word-scaled forward and backward offsets.
- Added `CPU::run(Memory&, max_instructions)`, which returns the number of
  executed instructions and bounds branch-to-self loops.
- Unsupported instruction encodings throw `std::runtime_error`; no unknown
  instruction is treated as a no-op.
- Extended CPU tests with canonical instruction words documented at each test
  site. The suite covers register results, SP behavior, PC updates, both
  branch directions, shifted immediates, XZR behavior, unsupported
  encodings, fetch bounds, zero limits, and execution limits.

### Validation

Configure and build:

```powershell
cmake -S WinIOS -B WinIOS\build -G "Visual Studio 18 2026" -A x64
cmake --build WinIOS\build --config Debug
```

Result: completed successfully with no warnings under `/W4 /permissive-`.

Tests:

```powershell
ctest --test-dir WinIOS\build -C Debug --output-on-failure
```

Result: `2/2` CTest tests passed. The updated CPU suite and the existing memory
suite both remained green.

Application:

```powershell
.\WinIOS\build\Debug\win_ios.exe
```

Result: exit code `0`; the existing demo output remained unchanged.

### Known limitations

This is an instruction-interpreter milestone, not a system emulator. There is
no loader, memory bus, MMU, permissions model, exception handling, flags,
other AArch64 instructions, firmware, or iOS support. It must not be used to
claim that WinIOS boots iOS, executes arbitrary ARM64 code, or runs iOS
applications.

## 2026-10-28 — Phase 6: tracing and basic debugger

### Work completed

- Added optional `CPU::TraceSink` tracing, disabled by default. Successful
  steps report the pre-execution PC, raw 32-bit word, decoded mnemonic and
  operands, post-execution PC, and changed register names.
- Added `Debugger`, a platform-neutral component that provides CPU/SP
  inspection, bounded guest-memory inspection, single-step execution, bounded
  continue, and trace enable/disable control.
- Added `win_ios.exe --debug`, while preserving the original default demo
  output. The debug demonstration runs verified `MOVZ X0, #42` and
  `ADD X1, X0, #5` words and prints trace/state/memory output.
- Added a dedicated debugger test executable covering trace enable/disable,
  trace contents, unchanged execution semantics, register and memory
  inspection, invalid ranges, single-step, bounded continuation, and
  branch-to-self behavior. Existing CPU and memory tests remain registered.

### Phase 6 validation

```powershell
cmake -S WinIOS -B WinIOS\build -G "Visual Studio 18 2026" -A x64
cmake --build WinIOS\build --config Debug
ctest --test-dir WinIOS\build -C Debug --output-on-failure
.\WinIOS\build\Debug\win_ios.exe
.\WinIOS\build\Debug\win_ios.exe --debug
```

MSVC build completed with no warnings under `/W4 /permissive-`. The final
CTest run covers `memory_tests`, `cpu_tests`, and `debugger_tests`; direct
debugger, CPU, and memory runs also pass. The default executable output is
unchanged. The debug mode only exercises the documented subset and does not
debug arbitrary AArch64 applications.

## 2026-10-28 — Phase 7: expanded instruction coverage

### Work completed

- Added 64-bit `MOVN` and `MOVK` wide-immediate instructions.
- Added 64-bit shifted-register `ORR` with LSL, LSR, and ASR shift types.
- Added non-flag-setting 64-bit `SUB` immediate with optional 12-bit shift.
- Added 64-bit unsigned-immediate `LDR` and `STR` with scaled offsets and
  SP-based addressing.
- Preserved tracing and added decoded records for all new instructions.
- Data loads read guest memory before changing the destination register.
  Stores read the source register before attempting the guest-memory write;
  bounds failures propagate and do not advance PC.
- Extended CPU and debugger tests with documented machine words, register-31
  behavior, shifted operands, memory results, invalid encodings, boundary
  failures, PC updates, and expanded trace disassembly.

### Supported subset after Phase 7

`MOVZ`, `MOVN`, `MOVK`, `ADD` immediate, `SUB` immediate, shifted-register
`ORR`, unsigned-immediate 64-bit `LDR`/`STR`, and unconditional `B`.

These are only the tested forms listed above. The project still has no loader,
MMU, permissions, condition flags, exception levels, system registers, or
general AArch64 compatibility, and it does not boot iOS or run iOS
applications.
