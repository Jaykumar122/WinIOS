# WinIOS

Windows 11 ARM64 emulation research project. This repository is a research
prototype, not a working iOS emulator.

## Status

Phase 4 baseline: a C++20 skeleton whose demonstration path computes
`X2 = 10 + 20 = 30`. It does not decode or execute ARM64 instructions yet.
See `docs/progress.md` for the recorded build and run output.

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
│   ├── loader/
│   ├── debugger/
│   └── app/
├── tests/
│   └── unit/
└── docs/
    └── progress.md
```

## Build and run

Visual Studio 2022/2026 (MSVC):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Debug
.\build\Debug\win_ios.exe
```

MinGW-w64 (a C++20-capable GCC 10+ is required):

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\win_ios.exe
```

Do not mix MSVC and MinGW build files in the same build directory. If you
change compilers, delete `build` and configure again.

Expected output:

```text
WinIOS v0.1.0
ARM64 emulation research prototype
Demo calculation: X2 = 30
CPU state demonstration complete.
```

## Roadmap

The phased plan lives in `../WinIOS_README.md` (Phase 1 through Phase 10).
Do not claim genuine iOS 26 compatibility without reproducible evidence, and
treat an IPSW as device-specific firmware rather than a bootable disk image.
