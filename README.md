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
  PC, NZCV value, and changed register names. The callback can write to a file
  or test buffer.

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

### Raw binary loading and execution

`BinaryLoader` accepts a non-empty raw binary containing little-endian,
32-bit AArch64 instruction words. It validates the complete file before
copying it into guest memory, checks aligned addresses and whole-instruction
size, and rejects destination ranges that do not fit. Failed loads do not
partially modify guest memory.

The loader does not parse ELF, Mach-O, IPA, IPSW, or any other executable or
firmware format. The entry address is a guest-memory address and is also the
load address in the command-line mode. CPU initialization resets registers,
SP, and PC, then sets PC to that entry address.

Run the committed raw fixture with a required instruction limit:

```powershell
.\build\Debug\win_ios.exe --run-binary .\examples\test_program.bin `
  --memory-size 65536 --entry 0 --max-instructions 5
```

Add `--trace` to print the existing instruction trace. The fixture executes
five supported instructions, producing `X0 = 47`, `X1 = 256`, and guest
memory at `0x100` equal to `47`.
To enable the host-side completion policy for this fixture, add
`--sentinel 24`; the final `B +8` reaches that address and reports
`completed`. Without a sentinel, the same five instructions report
`instruction_limit_reached`, not success. This policy does not alter the
architectural meaning of `B` or any other instruction.

The runtime layout uses the configured memory as flat guest RAM. The program
is loaded at `load_address` (the CLI uses `entry` for both), entry must be
inside the loaded bytes, and the default stack occupies the top 4096 bytes.
The stack pointer is initialized to the top of that region. The program and
stack must not overlap. CLI exit codes are 0 for `completed`, 2 for
`instruction_limit_reached`, and 1 for loading, configuration, memory, or
unsupported-instruction errors.

Runtime and CPU address arithmetic is checked for unsigned overflow and
underflow. A wrapped load/store address or branch target is reported as a
memory fault rather than being reused as a low guest address. Branches to
unmapped guest memory fail on the following fetch. A zero runtime instruction
limit is invalid; a positive limit reached exactly after the last successful
instruction reports `instruction_limit_reached` unless the sentinel policy
has completed execution. Each runtime invocation creates fresh guest memory
and CPU state.

### Implemented AArch64 instructions

The decoder currently supports this explicit table. `sf=1` denotes 64-bit
operands and `sf=0` denotes 32-bit operands where listed. Register 31 is
contextual: arithmetic immediate forms use SP, while logical/register forms
use XZR/WZR.

| Instruction form | Encoding family | Operands and effects | Flags / memory | Tests |
|---|---|---|---|---|
| MOVZ, MOVN, MOVK | wide-immediate | 32/64-bit `Wn`/`Xn`, shifts 0/16/32/48 (32-bit only 0/16) | no flags, no memory | CPU |
| ADD/SUB immediate | add/sub immediate | 32/64-bit, optional `LSL #12`, SP-aware | flags unchanged, no memory | CPU, loader, runtime |
| ADD/SUB shifted register | add/sub shifted-register | 32/64-bit, LSL/LSR/ASR | flags unchanged, no memory | CPU |
| ADDS/SUBS immediate/register | flag-setting add/sub families | 32/64-bit arithmetic; aliases CMP when destination is register 31 | updates NZCV | CPU |
| ORR shifted register | logical shifted-register | 32/64-bit, LSL/LSR/ASR, XZR/WZR | flags unchanged | CPU |
| TST alias | ANDS shifted-register | supported 32/64-bit shifted-register AND with discarded result | updates N and Z; C/V cleared | CPU |
| B | unconditional immediate | signed 26-bit word offset from instruction address | no flags, no memory | CPU, runtime |
| B.cond | conditional immediate | signed 19-bit word offset; all standard conditions | reads NZCV | CPU |
| CBZ/CBNZ | compare-and-branch | 32/64-bit register, signed 19-bit word offset | does not change NZCV | CPU |
| LDR/STR | unsigned immediate 64-bit | scaled offset, SP base support | no flags, guest memory access | CPU, loader, runtime |

Instruction words and shift encodings that are not in this subset throw an
explicit exception. LDR reads complete before a destination register changes;
STR reads its source before attempting the memory write. Fetches and data
accesses that fall outside guest memory propagate the memory bounds error
without silently changing state.

NZCV is exposed as a four-bit value using the architectural bit order
`N=bit 3, Z=bit 2, C=bit 1, V=bit 0`. `reset()` clears NZCV. Reference words
in the instruction tests are documented beside each test and were verified
against the AArch64 encoding layout; no external assembler or differential
reference emulator is required to build or test this project.

### Tests (`tests/unit/`)

A small custom harness (`test_harness.h`) reports failures with file, line,
and expression, and exits nonzero on failure. It does not depend on
GoogleTest, and it does not use `assert()`, so `NDEBUG` cannot disable checks.

## Not implemented

- Program loader, memory bus connecting `CPU` to `Memory`, MMU/permissions.
- The runtime layer provides explicit guest configuration and structured
  execution statuses, but it is not an operating-system runtime.
- Other AArch64 instructions, exception levels, and system registers.
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
├── examples/
│   ├── test_program.S
│   └── test_program.bin
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
.\build\Debug\debugger_tests.exe
.\build\Debug\loader_tests.exe
.\build\Debug\runtime_tests.exe
```

The negative tests cover maximum-sized addresses, end-of-memory and
misaligned binary loads, invalid entry/stack/sentinel regions, branch target
faults, arithmetic overflow and underflow, exact instruction limits,
unsupported instructions, memory faults, and repeated runtime execution.

To configure without tests, pass `-DBUILD_TESTING=OFF`.

## Roadmap

The phased plan is kept outside this repository at
`D:\WinISO26\WinIOS_README.md`. This phase corresponds to roadmap Phase 5.
The next milestone is a small, documented instruction interpreter with
known-good encodings, not full-system emulation.
