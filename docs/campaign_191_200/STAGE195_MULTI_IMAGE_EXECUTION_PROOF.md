# WINIOS STAGE 195: DYLD AND MULTI-IMAGE EXECUTION FORENSICS
## Independent Forensics of the Tri-Image Execution Pipeline: Main -> libMath(add) -> Main -> libSystem(exit)

**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 Pro x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Deliverable Document:** `docs/campaign_191_200/STAGE195_MULTI_IMAGE_EXECUTION_PROOF.md`

---

### 1. Architectural Scope & Pipeline Definition

Stage 195 independently verifies the live guest execution of the tri-image execution pipeline:
$$\text{Main Executable (0x1000)} \xrightarrow{\text{blr x16}} \text{libMath.dylib (0x6000)} \xrightarrow{\text{ret}} \text{Main Executable (0x1010)} \xrightarrow{\text{blr x16}} \text{libSystem.B.dylib (0x7000)} \xrightarrow{\text{svc \#0x80}} \text{SYS\_exit(42)}$$

The primary audit objective is to verify that:
1. All three images reside in guest memory at authentic virtual addresses.
2. Cross-image function invocations are achieved through real ARM64 branch instructions (`BLR` / `RET`), not host callbacks.
3. Chained fixups patch real guest memory pointers to target function entry points.
4. Calculation and parameter passing occur through ARM64 registers (`X0`, `X1`, `X16`, `LR`).

---

### 2. Tri-Image Topology and Guest Layout

| Image Identity | Role | Guest Base VAddr | Segment | VAddr Range | Permissions | Exported Symbols |
| :--- | :--- | :---: | :--- | :---: | :---: | :--- |
| `main_executable` | Primary Driver | `0x00001000` | `__TEXT` | `0x00001000 - 0x00001030` | `r-x` | `_main` |
| `libMath.dylib` | Dynamic Library | `0x00006000` | `__TEXT` | `0x00006000 - 0x00006010` | `r-x` | `_add_integers` |
| `libSystem.B.dylib`| System Runtime | `0x00007000` | `__TEXT` | `0x00007000 - 0x00007010` | `r-x` | `_exit` |

---

### 3. Step-by-Step Guest PC and Register Trace

```text
========================================================================================
Step  Image              PC       Instruction Hex  Disassembly         Register Mutation
========================================================================================
1     main_executable    0x001000 D2800320         mov x0, #25         X0  = 25 (0x19)
2     main_executable    0x001004 D2800221         mov x1, #17         X1  = 17 (0x11)
3     main_executable    0x001008 580000B0         ldr x16, [pc, #20]  X16 = 0x6000 (libMath._add_integers)
4     main_executable    0x00100C D63F0200         blr x16             LR  = 0x1010, PC -> 0x6000
----------------------- [BOUNDARY CROSSING: Main -> libMath] ---------------------------
5     libMath.dylib      0x006000 8B010000         add x0, x0, x1      X0  = 42 (0x2A) [25 + 17]
6     libMath.dylib      0x006004 D65F03C0         ret                 PC -> LR (0x1010)
----------------------- [BOUNDARY CROSSING: libMath -> Main] ---------------------------
7     main_executable    0x001010 580000B0         ldr x16, [pc, #20]  X16 = 0x7000 (libSystem._exit)
8     main_executable    0x001014 D63F0200         blr x16             LR  = 0x1018, PC -> 0x7000
----------------------- [BOUNDARY CROSSING: Main -> libSystem] -------------------------
9     libSystem.B.dylib  0x007000 D2800030         mov x16, #1         X16 = 1 (SYS_exit)
10    libSystem.B.dylib  0x007004 D4001001         svc #0x80           Trap to Darwin kernel with X0 = 42
========================================================================================
```

---

### 4. Forensic Verification Analysis

1. **No Host-Side Shortcuts:**
   - The addition of `25 + 17 = 42` was performed by the ALU inside `CPU::step()` executing opcode `0x8B010000` (`add x0, x0, x1`) at PC `0x6000`.
   - The return to the caller was performed by `CPU::step()` executing opcode `0xD65F03C0` (`ret`), popping the return address from register `LR` (`0x1010`).
2. **Two-Level Namespace Isolation:**
   - The main binary references `_add_integers` from ordinal 2 (`libMath`) and `_exit` from ordinal 1 (`libSystem`).
   - If `libMath` also defined an internal symbol `_exit`, the two-level namespace binding guarantees that the main executable binds strictly to ordinal 1's `_exit`.
3. **Chained Fixup Memory Patching:**
   - Prior to execution, the pointer slot `0x101C` contained an encoded fixup target resolved by `DyldEngine::process_chained_fixups`, which wrote the absolute virtual target `0x6000`.
   - Similarly, slot `0x1024` was patched with `0x7000`.
4. **Clean Exit:**
   - The final `svc #0x80` in `libSystem` delivered exception `ESR_EL1_EC_SVC64`.
   - `DarwinUserspaceEngine` halted the virtual core and returned exit code 42 with zero runtime errors.

---

### 5. Conclusion

The multi-image pipeline execution proof confirms that WinIOS supports true cross-image dynamic linking, branch instruction traversal, and ABI register conventions across distinct Mach-O image boundaries in userspace.
