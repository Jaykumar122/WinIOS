# WinIOS: AArch64 / Darwin Virtual Platform & Execution Environment

[![C++20](https://img.shields.io/badge/C%2B%2B-20%20(MSVC)-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Platform](https://img.shields.io/badge/Platform-Windows%2011%20x64-0078D6.svg)](https://www.microsoft.com/windows)
[![Compiler Policy](https://img.shields.io/badge/Compiler-%2FW4%20%2FWX%20%2Fpermissive---purple.svg)]()
[![Target](https://img.shields.io/badge/Target-iPhone%2015%20(iPhone15%2C4%20%2F%20D37AP%20%2F%20A16%20T8120)-orange.svg)]()
[![Tests](https://img.shields.io/badge/CTest-152%2B%20Suites%20Passing%20(100%25)-brightgreen.svg)]()
[![Execution](https://img.shields.io/badge/Execution-Pure%20Guest%20CPU%20(EL0)-success.svg)]()

WinIOS is an experimental, high-assurance AArch64 (ARM64 / ARM64e) interpreter, Darwin userspace execution environment, and deterministic virtual platform engineered in modern C++20 for Windows 11 x64.

---

## 1. About This Project

WinIOS provides an instruction-accurate, cycle-disciplined foundation for AArch64 and Apple Darwin software execution directly on Windows. Designed from first principles with an emphasis on strict determinism, formal state verification, and memory safety, WinIOS models the hardware identity and execution environment of modern Apple silicon devices—specifically targeting the **iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)**.

The platform eliminates undefined host dependencies by routing all architectural time, memory transitions, and userspace traps through an authoritative virtual CPU and runtime coordination pipeline.

---

## 2. Core Architectural Subsystems

### 2.1 Darwin Userspace Engine (`DarwinUserspaceEngine`)
- **EL0 Virtual CPU Execution:** Executes guest ARM64 opcodes step-by-step through `Arm64CpuCore` without host callbacks or synthetic shortcuts.
- **Darwin Stack Frame Construction:** Formats standard 16-byte aligned userspace initial stack frames containing `argc`, `argv`, `envp`, and the `apple` vector (`execpath`, `stack_guard`, `entropy`).
- **Instruction Accounting:** Explicit tracking of `guest_instructions_retired` at EL0, cleanly decoupled from host-side loader and trap overhead.

### 2.2 Mach-O Loader & Virtual Memory Model
- **16 KB Page Granule:** Strict enforcement of Apple's 16 KB virtual memory page size (`0x4000`).
- **W^X Memory Enforcement:** Strict permission transitions preventing concurrent write and execute privileges:
  - `__TEXT`: Read + Execute (`PROT_READ | PROT_EXEC`).
  - `__DATA`: Read + Write (`PROT_READ | PROT_WRITE`).
  - `__LINKEDIT`: Read-only (`PROT_READ`).
- **ASLR Engine:** Deterministic base slide calculations applied across all loaded segments and dynamic libraries.

### 2.3 ARM64e Chained Fixups & PAC Decoupling
- **Apple Specification Conformance:** Parser fully conforms to Apple Darwin `mach-o/fixup-chains.h`.
- **Supported Pointer Formats:** `DYLD_CHAINED_PTR_64`, `DYLD_CHAINED_PTR_64_OFFSET`, `DYLD_CHAINED_PTR_ARM64E`, `DYLD_CHAINED_PTR_ARM64E_USERLAND`, and `DYLD_CHAINED_PTR_ARM64E_USERLAND24`.
- **Software PAC Handling:** Pointer Authentication Code (PAC) diversity and signature tags are unchained and verified in software without requiring host hardware PAC extensions.
- **Defensive Unchaining:** Circular pointer chain detection to prevent infinite loops on corrupted or adversarial images.

### 2.4 Dynamic Linking & Multi-Image Execution Pipeline
- **Export Trie Traversal:** Resolves symbols from Mach-O LC_DYLD_INFO and LC_DYLD_EXPORTS_TRIE segments.
- **Ordinal Binding:** Dynamically binds two-level namespace ordinal imports against dependent dynamic libraries (`.dylib`).
- **Cross-Image Branch Tracking:** Verified cross-binary instruction jumping (`BLR`/`RET`) across disparate virtual memory regions with register integrity (`LR` / `X30` preserved).

### 2.5 Darwin Syscall & Mach IPC ABI
- **Unified Exception Trap:** Uses `SVC #0x80` exception syndrome (`0x15`).
- **ABI Conventions:**
  - **BSD Syscalls:** Register $X16 > 0$. Arguments in $X0..X5$. Errors returned in $X0$ with the `NZCV` Carry flag set ($C = 1$). Success returns with $C = 0$.
  - **Mach Traps:** Register $X16 < 0$ (e.g. $X16 = -31$ for `mach_msg_trap`, $X16 = -26$ for `mach_reply_port`).
- **Mach IPC Subsystem:** Concrete implementation of Mach ports, port rights, message queueing, and `mach_msg` buffer staging. Hardened against out-of-bounds pointers and buffer overruns.
- **Darwin VFS:** POSIX/Darwin file descriptor isolation starting at fd 3 (`0, 1, 2` reserved for standard streams), sandbox path isolation, and atomic file operations.

### 2.6 Virtual Platform & Apple Hardware Profiles
- **Hardware Profile:** iPhone 15 (`D37AP`, Apple A16 Bionic `T8120`, 6 cores: 2 Everest performance + 4 Sawtooth efficiency cores).
- **DeviceTree:** Apple DeviceTree traversal, node parsing, and property decoding.
- **Interrupt & Power Management:** Apple Interrupt Controller 2 (AIC2) interrupt routing and Apple Power Manager (PMGR) device state modeling.
- **Storage Subsystem:** Apple ANS2 NVMe storage interface and APFS container parsing foundations.

---

## 3. Architectural Topology

```text
+---------------------------------------------------------------------------------------+
|                                WinIOS Virtual Platform                                |
|   Target: iPhone 15 (iPhone15,4 / D37AP / Apple A16 Bionic T8120 / ARM64e)            |
+-------------------------------------------+-------------------------------------------+
                                            |
                 +--------------------------+---------------------------+
                 |                                                      |
                 v                                                      v
   +---------------------------+                          +---------------------------+
   |   Mach-O / dyld Engine    |                          |  Apple Hardware Profiles  |
   | - 16KB W^X Page Granule   |                          | - DeviceTree Traversal    |
   | - ARM64e Chained Fixups   |                          | - AIC2 Interrupt Model    |
   | - Export Trie & ASLR      |                          | - PMGR Power Coordination |
   | - Multi-Image Binding     |                          | - ANS2 NVMe & APFS Parser |
   +-------------+-------------+                          +-------------+-------------+
                 |                                                      |
                 +--------------------------+---------------------------+
                                            |
                                            v
+---------------------------------------------------------------------------------------+
|                         DarwinUserspaceEngine (Guest EL0 CPU)                         |
|                                                                                       |
| 1. Setup canonical Darwin stack frame (argc, argv, envp, apple vector).               |
| 2. Fetch ARM64 opcodes from guest virtual memory (__TEXT segment).                    |
| 3. Step instructions via Arm64CpuCore, advancing guest_instructions_retired.          |
| 4. Trap SVC #0x80 exceptions:                                                         |
|    - If X16 > 0  --> Dispatch to DarwinBsdSubsystem (open, read, write, exit, etc.)  |
|    - If X16 < 0  --> Dispatch to MachIpcSubsystem (mach_msg_trap, ports, IPC queues)  |
| 5. Return status via Apple calling convention (X0 result, NZCV Carry flag on error).  |
+---------------------------------------------------------------------------------------+
```

---

## 4. Repository Layout

```text
WinIOS/
├── CMakeLists.txt              # Top-level build configuration (MSVC C++20, /W4 /WX)
├── README.md                   # Project overview, architecture, and guides
├── .gitignore                  # Git ignore rules (excluding reports, docs, artifacts)
├── src/
│   ├── app/                    # Session CLIs (Darwin boot, Linux boot, ELF session)
│   ├── cpu/                    # AArch64 CPU core, MMU, sysreg, PAC, debug architecture
│   ├── device/                 # Apple AIC, ANS2, DeviceTree, PMGR, virtual UART
│   ├── firmware/               # Virtual firmware, Apple boot-args, boot configuration
│   ├── fs/                     # APFS parser, Darwin VFS, file descriptors
│   ├── graphics/               # WindowServer, display surfaces, framebuffer presentation
│   ├── loader/                 # Mach-O loader, chained fixups, dyld, real artifact harness
│   ├── memory/                 # Physical memory maps, bounds checking, 16KB paging
│   ├── reference/              # Independent architectural reference models
│   ├── replay/                 # Deterministic replay coordinator, session state hashing
│   ├── runtime/                # DarwinUserspaceEngine, Mach IPC, BSD syscalls, IOKit
│   ├── snapshot/               # Architectural snapshot serializer (.wsnap)
│   ├── sndl/                   # Nondeterministic event recording pipeline (.sndl)
│   └── trace/                  # Execution diagnostics, instruction trace inspector (.wtr)
└── tests/
    ├── unit/                   # Unit test suites (152+ suites across Stages 105-200)
    ├── integration/            # Multi-component integration test pipelines
    └── support/                # Mach-O corpus builders and test fixtures
```

---

## 5. Building and Running

### Prerequisites
- **Operating System:** Windows 10/11 x64 (Windows 11 Pro recommended)
- **Toolchain:** Visual Studio 2022 or 2026 (MSVC C++20 with strict `/W4 /WX /permissive-` policy)
- **Build System:** CMake 3.25 or higher

### Build Instructions (PowerShell)

```powershell
# 1. Configure the build directory
cmake -S . -B build -G "Visual Studio 18 2026" -A x64

# 2. Compile the project in Release configuration (0 warnings treated as errors)
cmake --build build --config Release

# 3. Compile the project in Debug configuration
cmake --build build --config Debug
```

---

## 6. Running Test Suites

WinIOS maintains 152+ CTest test targets with a **100% pass rate** in both Release and Debug configurations:

```powershell
# Run the complete test suite
ctest --test-dir build -C Release --output-on-failure

# Execute the latest Darwin execution verification suites (Stages 170-199)
ctest --test-dir build -C Release -R "stage1[7-9][0-9]_.*" --output-on-failure

# Run the deterministic replay and reliability test executable directly
.\build\Release\stage199_replay_and_reliability_tests.exe
```

---

## 7. Execution Capability & Hardware Boundary Matrix

| Capability | Status | Implementation Details / Hardware Boundary |
| :--- | :---: | :--- |
| **Synthetic ARM64 Execution** | **OPERATIONAL** | Pure virtual CPU instruction stepping at EL0/EL1 |
| **Open-Source Mach-O Admission** | **OPERATIONAL** | Full LC command parsing from standard LLVM/Clang outputs |
| **Open-Source Mach-O Execution** | **OPERATIONAL** | Pure EL0 execution without host callbacks (e.g. 405 instructions retired in `corpus_math_compute`) |
| **Multi-Image Dynamic Linking** | **OPERATIONAL** | Tri-image call chain (`Main` $\to$ `libMath` $\to$ `libSystem`) with genuine `BLR`/`RET` |
| **ARM64e Chained Fixups & PAC** | **OPERATIONAL** | Apple `fixup-chains.h` unchaining and software PAC verification |
| **dyld Shared Cache (Levels 1–3)** | **OPERATIONAL** | Header parsing (`dyld_v1  arm64e`), mapping validation, image cataloging |
| **Monolithic dyld Shared Cache (Level 7)** | **BLOCKED** | Requires complete Objective-C selector and class relocation infrastructure |
| **Genuine Encrypted Apple Binaries** | **BLOCKED** | Protected by `LC_ENCRYPTION_INFO_64` requiring Apple Secure Enclave (SEP) hardware GID keys |
| **Stock Apple Kernel Collections** | **BLOCKED** | Encapsulated in Image4 manifests tied to hardware TrustChain and personalized APTickets |

---

## 8. License & Attribution

This project is an independent research endeavor focused on processor architecture, operating system runtime emulation, and high-assurance execution engineering on Windows. 

- Apple, iPhone, iOS, Darwin, XNU, and macOS are registered trademarks of Apple Inc.
- ARM, AArch64, and ARM64 are registered trademarks of Arm Limited.
- This platform does not ship with, decrypt, or distribute proprietary Apple firmware or copyrighted system artifacts.
