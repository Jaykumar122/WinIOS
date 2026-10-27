# WinIOS

Windows 11 ARM64 emulation research project. This repository is an
experimental foundation, not a working iOS emulator.

## Status

The project now has a bounds-checked guest-memory model and a small,
CTest-covered AArch64 interpreter. It still cannot boot iOS or run iOS apps.
The `X2 = 30` output remains a host-side demonstration of register state.

See `docs/progress.md` for the recorded build, test, and run output.

## Implemented

### Guest memory (`src/memory/`)

- `Memory(std::size_t size)` allocates flat guest RAM initialized to zero.
- A zero-byte configuration throws `std::invalid_argument`.
- Byte (`read_u8` / `write_u8`), 32-bit (`read_u32` / `write_u32`), and
  64-bit (`read_u64` / `write_u64`) accesses.
- Multi-byte values use little-endian byte order regardless of host order.
- Every access is bounds-checked before touching the vector; invalid or
  boundary-crossing accesses throw `std::out_of_range`.
- The range check rejects `offset > size` before computing `size - offset`, so
  huge offsets cannot wrap around.
- Unaligned accesses are currently allowed; alignment and permissions are not
  modeled yet.

### CPU (`src/cpu/`)

- 31 general-purpose registers (`X0`..`X30`) and a 64-bit program counter.
- A separate 64-bit stack pointer is used where an instruction encoding names
  register 31 as `SP`.
- `MOVZ` treats destination register 31 as `XZR` and discards the result;
  `ADD (immediate)` treats register 31 as `SP` for both operands.
- `reset()` clears every register, the stack pointer, and the program counter.
- `read_register(index)` / `write_register(index, value)` are checked; indices
  outside `0..30` throw `std::out_of_range`.
- `get_program_counter()` / `set_program_counter()`.
- `step(Memory&)` fetches one little-endian 32-bit instruction at the current
  PC and executes the supported subset below. The initial and reset PC is zero;
  ordinary instructions advance it by four bytes.
- `run(Memory&, max_instructions)` executes at most the requested number of
  instructions and returns the count executed, so branch-to-self code is
  bounded.
- Optional instruction tracing uses `CPU::TraceSink`; it is disabled by
  default and receives the pre-execution PC, raw word, decoded text, resulting
  PC, and changed register names. The callback can write to a file or test
  buffer.

### Debugger

`Debugger` in `src/debugger/` is a small API around an existing `CPU` and
`Memory`. It can inspect all registers and SP, inspect a validated memory
range, execute one instruction, continue for a bounded count, and enable or
disable a trace sink. Invalid memory ranges throw `std::out_of_range`, and
unsupported instructions retain the CPU's explicit `std::runtime_error`.

The executable keeps its default demo behavior. A built-in demonstration
debug session can be run with:

```powershell
.\build\Debug\win_ios.exe --debug
```

It prints trace records in this form:

```text
PC=0x0 WORD=0xd2800540 MOVZ X0, #42 PC_AFTER=0x4
```

The debugger is intended for this small interpreter only; it is not a
breakpoint engine, GUI, remote debugger, or arbitrary-application debugger.

### Implemented AArch64 instructions

- 64-bit `MOVZ` (wide immediate), including all valid halfword shifts.
- 64-bit, non-flag-setting `ADD` (immediate), including the optional 12-bit
  shift and the instruction-specific `XZR`/`SP` register-31 semantics.
- `B` (unconditional immediate), with signed 26-bit word offsets relative to
  the branch instruction address.

Instruction words that are not in this subset, including unsupported
encodings, throw an explicit exception. Fetches that fall outside guest
memory propagate the memory bounds error rather than being ignored.

### Tests (`tests/unit/`)

A small custom harness (`test_harness.h`) reports failures with file, line,
and expression, and exits nonzero on failure. It does not depend on
GoogleTest, and it does not use `assert()`, so `NDEBUG` cannot disable checks.

## Not implemented

- Program loader, memory bus connecting `CPU` to `Memory`, MMU/permissions.
- Other AArch64 instructions, condition flags, exception levels, and system
  registers.
- Breakpoints, GUI, remote debugging, and tracing beyond the supported
  instruction subset.
- Any iOS firmware, IPSW, or device emulation. Genuine iOS 26 boot is an open
  feasibility question, not a supported feature.

## Layout

```text
WinIOS/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── cpu/
│   │   ├── cpu.h
│   │   └── cpu.cpp
│   ├── memory/
│   │   ├── memory.h
│   │   └── memory.cpp
│   ├── loader/
│   ├── debugger/
│   └── app/
├── tests/
│   └── unit/
│       ├── test_harness.h
│       ├── memory_tests.cpp
│       └── cpu_tests.cpp
└── docs/
    └── progress.md
```

## Build and run

Visual Studio 2022/2026 (MSVC), from the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\Debug\win_ios.exe
```

MinGW-w64 needs a C++20-capable GCC 10+; the MinGW.org GCC 6.3.0 sometimes
found on `PATH` is too old:

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
.\build-mingw\win_ios.exe
```

Do not mix MSVC and MinGW build files in the same build directory.

Expected program output:

```text
WinIOS v0.1.0
ARM64 emulation research prototype
Demo calculation: X2 = 30
CPU state demonstration complete.
```

## Tests

CTest is enabled through `include(CTest)` and `BUILD_TESTING`. Build the test
executables with the normal build, then run:

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

Individual suites:

```powershell
.\build\Debug\memory_tests.exe
.\build\Debug\cpu_tests.exe
```

To configure without tests, pass `-DBUILD_TESTING=OFF`.

## Roadmap

The phased plan is kept outside this repository at
`D:\WinISO26\WinIOS_README.md`. This phase corresponds to roadmap Phase 5.
The next milestone is a small, documented instruction interpreter with
known-good encodings, not full-system emulation.
