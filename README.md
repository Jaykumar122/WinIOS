# WinIOS

Windows 11 ARM64 emulation research project. This repository is an
experimental foundation, not a working iOS emulator.

## Status

The project now has a bounds-checked guest-memory model, a checked AArch64
general-purpose register file with reset support, and CTest-registered unit
tests. It still does **not** fetch, decode, or execute ARM64 instructions, and
it cannot boot iOS or run iOS apps. The `X2 = 30` output remains a host-side
demonstration of register state.

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
- `reset()` clears every register and the program counter.
- `read_register(index)` / `write_register(index, value)` are checked; indices
  outside `0..30` throw `std::out_of_range`.
- `get_program_counter()` / `set_program_counter()`.
- `run_demo()` is explicitly a register-state demonstration, not an
  instruction interpreter.

### Tests (`tests/unit/`)

A small custom harness (`test_harness.h`) reports failures with file, line,
and expression, and exits nonzero on failure. It does not depend on
GoogleTest, and it does not use `assert()`, so `NDEBUG` cannot disable checks.

## Not implemented yet

- ARM64 instruction decoding and execution (`MOVZ`, `ADD`, `B`, ...).
- Program loader, memory bus connecting `CPU` to `Memory`, MMU/permissions.
- Debugger, tracing, CLI controls, or GUI.
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
