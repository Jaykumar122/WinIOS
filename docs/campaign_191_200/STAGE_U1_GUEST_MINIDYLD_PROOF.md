# WINIOS CAMPAIGN 191-U — STAGE U1: GUEST-SIDE MINI-DYLD PROOF

**Campaign:** 191-U  
**Stage:** U1 — Freestanding Guest-Side Dynamic Linker (`minidyld`)  
**Status:** PASS (9/9 unit & artifact execution tests passing in `stage_u1_guest_minidyld_tests`)  

---

## 1. Objective & Provenance

In previous campaigns (`191-S` / `191-T`), Mach-O dynamic linking (loading dylibs, traversing export tries, patching GOT/stub slots, and invoking `__mod_init_func` initializers) was orchestrated by host C++ code in `DyldEngine` and `UserspaceMachOLoader`.

Stage U1 moves dynamic-linker orchestration into EL0 by compiling an original freestanding ARM64 dynamic linker (`tests/artifacts/src/minidyld.c`) into a static Mach-O executable (`tests/artifacts/bin/minidyld`) using Zig/Clang (`zig cc -target aarch64-macos`), executing image resolution, export-trie lookup, relocation/binding, and constructor dispatch on the WinIOS guest CPU.

> [!NOTE]
> **What remains host-emulated:** BSD syscalls (`SYS_open`, `SYS_pread`, `SYS_mmap`, `SYS_mprotect`, `SYS_write`, `SYS_exit`, etc.), Mach IPC traps, process/VFS state (`DarwinBsdSubsystem`), and mapping of the initial images (main Mach-O + `minidyld` segments and initial Darwin stack setup in `execute_macho_with_guest_dyld`) remain host-emulated.

### Artifact Provenance
| Property | Value |
| :--- | :--- |
| **Source File** | `tests/artifacts/src/minidyld.c` (original freestanding C implementation; zero Apple/APSL code) |
| **Binary File** | `tests/artifacts/bin/minidyld` |
| **Compiler Command** | `zig cc -target aarch64-macos -O2 -nostdlib -fno-builtin -fno-stack-protector -mno-unaligned-access -Wl,-e,_start -Wl,-pagezero_size,0x02000000` |
| **Size** | 67,512 bytes (updated in Stage V0 for `LC_DYLD_CHAINED_FIXUPS`; was 67,320 B in `d2b2983`) |
| **SHA-256** | `02d6002816ede71f2cd80c453ae072aecc406ae65f57ab5d02c8c08b949a3891` (`d2b2983`: `ca28f68b19f6d6150511c629914dd90e750dd2180580476c7eb102d6c5bcd838`) |
| **Entry Point** | `0x0200414C` (`_start`) |
| **External Dylib Imports** | 0 (completely self-contained) |

---

## 2. Guest Runtime & Syscall Extensions

To enable `minidyld` to load dylibs from the guest VFS via syscalls:
1. **`SYS_pread` (BSD syscall `153`)**: Added `DarwinBsdSubsystem::pread(...)` and wired `nr == 153` in `DarwinUserspaceEngine::handle_bsd_syscall` to read Mach-O headers, load commands, segments, and `__LINKEDIT` export tries at explicit file offsets without mutating descriptor seek state.
2. **`SYS_mprotect` (BSD syscall `74`)**: Updated `DarwinBsdSubsystem::mprotect` to support sub-range protection transitions on dylib regions mapped by guest code.
3. **`DarwinUserspaceEngine::execute_macho_with_guest_dyld(...)`**:
   - Maps only the main Mach-O segments and `minidyld` segments into guest RAM.
   - Deliberately does **not** load any dependent dylibs and does **not** perform symbol binding on the host.
   - Passes the mapped main `mach_header_64` address in `X0` and `initial_sp` in `X1`, and transitions CPU to EL0 at `minidyld`'s entry point (`0x02003C1C`).

---

## 3. CPU Decoder & Alignment Findings Driven by Real Artifacts

Executing `minidyld` and `ctor_main` uncovered concrete instruction gaps and alignment constraints in WinIOS CPU execution:
1. **Strict Natural Alignment (MMU Disabled)**: `cpu.cpp` enforces natural alignment when MMU is off. Clang's vectorization of unaligned static arrays caused an Alignment Fault (`ESR=0x92000061`, `FAR=0x20094a4`). Remediated by aligning state structures with `__attribute__((aligned(16)))` and compiling with `-mno-unaligned-access`.
2. **Advanced SIMD Modified Immediate (`MOVI` / `MVNI` / `ORR` / `BIC`)**: Clang emitted `0x6F00E400` (`MOVI V0.16B, #0`) to zero stack buffers. Previously, `cpu.cpp` rejected `immh == 0` with `Reserved immh in shift immediate`. Implemented full ARMv8 Advanced SIMD modified immediate decoding per ARM DDI 0487.
3. **SIMD/FP Register Pair Load/Store (`STP` / `LDP` for `Q`/`D`/`S`)**: Clang emitted `STP Q0, Q0, ...` (`0xAD...`) following `MOVI`. Added 128-bit (`Q`), 64-bit (`D`), and 32-bit (`S`) pair load/store execution and disassembly in `cpu.cpp`.
4. **Add/Subtract Extended Register (`CMP X25, W8, SXTW` / `0xEB28C33F`)**: `ctor_main` emitted extended-register comparison (bit 21 = 1). Implemented `kAddSubExtendedRegister` in `cpu.cpp` supporting all 8 extension options (`UXTB`, `UXTH`, `UXTW`, `UXTX`, `SXTB`, `SXTH`, `SXTW`, `SXTX`).
5. **Post-Order Topological Initializer Execution**: `minidyld` was revised to record `g_init_order` after recursive transitive dependency loading, ensuring leaf constructors (`libctor_leaf`) execute strictly before middle (`libctor_mid`) and main (`ctor_main`), matching Darwin dyld semantics regardless of `LC_LOAD_DYLIB` declaration order.

---

## 4. Verification Results (`stage_u1_guest_minidyld_tests`)

All 9 tests pass with 0 failures:

| Test | Target / Scenario | Instructions Retired | BSD Syscalls | Output / Verification | Result |
| :--- | :--- | :---: | :---: | :--- | :--- |
| **Test 1** | `SYS_pread` (153) Subsystem | N/A | Unit API | Non-advancing offset reads verified; pipes rejected | **PASS** |
| **Test 2** | `minidyld` Provenance | N/A | N/A | 67,512 B (67,320 B at `d2b2983`), SHA-256 verified, 0 dylib imports | **PASS** |
| **Test 3** | `REAL-005` Two-Image (`main_with_dylib` -> `libmathreal`) | 9,136 | 22 | `stdout="DYLIB_OK: 0x0A11C001CAFEBAE8\n"`, exit `0` | **PASS** |
| **Test 4** | `REAL-009` Three-Image Chain (`chain_main` -> `libmid` -> `libleaf`) | 9,938 | >20 | `stdout="CHAIN_OK: 142\n"`, exit `0` | **PASS** |
| **Test 5** | Sequential Constructors (`ctor_main` -> `mid` -> `leaf`) | 12,818 | >20 | `stdout="CTOR_CHAIN_OK: 1-2-3\n"`, exit `0` | **PASS** |
| **Test 6** | Differential Memory Proof (Guest GOT inspection) | 9,136 | 22 | `GOT[_real_add]=0x21470`, `GOT[_real_read_global]=0x21490` | **PASS** |
| **Test 7** | Negative: Missing Dylib | >2,000 | >5 | Deterministic guest `sys_exit(127)` | **PASS** |
| **Test 8** | Negative: Corrupted Dylib Header | >2,000 | >5 | Deterministic guest `sys_exit(126)` | **PASS** |
| **Test 9** | Independent CPU Unit Tests (`MOVI`, `STP/LDP Q`, `CMP SXTW`) | 5 | 0 | Register/memory/NZCV states verified per instruction | **PASS** |

---

## 5. Evidence Classification
- **Guest Dynamic Linker (`minidyld`)**: `REAL-ARTIFACT-EXECUTED` (all linking, VFS reading, export trie lookups, GOT binding, constructor dispatch, and jump to `LC_MAIN` retired as genuine EL0 guest instructions).
