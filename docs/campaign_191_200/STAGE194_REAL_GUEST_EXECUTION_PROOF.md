# WINIOS STAGE 194: REAL ENTRY-POINT AND GUEST CPU EXECUTION PROOF
## Execution Verification, Entry Point Calculations, Register States, and Instruction Retirement

**Target:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120` / ARM64e)  
**Host Toolchain:** Windows 11 Pro x64, Visual Studio 18 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Deliverable Document:** `docs/campaign_191_200/STAGE194_REAL_GUEST_EXECUTION_PROOF.md`

---

### 1. Entry Point Resolution & Architecture Conformance

The Darwin Userspace Loader resolves entry addresses strictly conforming to Apple dyld specification:

#### A. `LC_MAIN` Entry Offset Calculation
- Load command `kCmdMain` (`0x80000028u` / `LC_MAIN`) encodes an unsigned 64-bit file offset: `m->entryoff`.
- **Dyld Conformance Rule:** `entryoff` is **not** an absolute virtual address; it is the byte offset of the `main()` function relative to the `__TEXT` segment (`mach_header_64`).
- **Calculation in WinIOS:**
  $$\text{entry\_point} = \text{text\_vmaddr} + \text{entryoff} + \text{aslr\_slide}$$
  For standard binaries:
  $$\text{entry\_point} = \text{0x100000000} + \text{0x1000} + \text{slide} = \text{0x100001000} + \text{slide}$$

#### B. Initial Register Setup (ARM64 Darwin ABI)
Prior to the first guest CPU instruction retirement, registers are initialized:
- `PC` = Calculated userspace entry point (`0x100001000ULL + slide`)
- `SP_EL0` = 16-byte aligned stack pointer pointing to `argc`
- `X0` = `argc` (argument count)
- `X1` = `argv` (pointer to array of null-terminated argument strings)
- `X2` = `envp` (pointer to array of environment strings)
- `X3` = `apple` (pointer to Apple vector: `executable_path`, page size, dyld info)
- `CurrentEL` = 0 (Userspace / EL0)

---

### 2. Live Instruction Retirement Proof: `corpus_math_compute`

The iterative computation artifact `corpus_math_compute` executes an algorithmic loop computing the triangular number $\sum_{i=1}^{100} i = 5050$:

```text
Assembly Listing at Entry (PC: 0x100001000):
0x100001000: D2800000  mov x0, #0            ; Accumulator = 0
0x100001001: D2800021  mov x1, #1            ; Counter i = 1
0x100001008: 8B010000  add x0, x0, x1        ; [Loop] Accumulator += i
0x10000100C: 91000421  add x1, x1, #1        ; i += 1
0x100001010: F101943F  cmp x1, #101          ; Compare i with 101
0x100001014: 54FFFFA1  b.ne -3               ; Loop while i <= 100 (target: 0x100001008)
0x100001018: D2800000  mov x0, #0            ; Exit code = 0
0x10000101C: D2800030  mov x16, #1           ; Syscall SYS_exit
0x100001020: D4001001  svc #0x80             ; Trap to Darwin BSD subsystem
```

#### Forensic Execution Verification:
1. **Total Guest Instructions Retired:** Exactly **405 instructions**:
   - 2 setup instructions (`mov x0`, `mov x1`)
   - 100 loop iterations $\times$ 4 instructions per iteration (`add`, `add`, `cmp`, `b.ne`) = 400 instructions
   - 3 exit sequence instructions (`mov x0`, `mov x16`, `svc #0x80`)
   - Total: $2 + 400 + 3 = 405$ instructions.
2. **Intermediate Register State:**
   - At iteration 100: `X0 = 5050` (`0x13BA`), `X1 = 101` (`0x65`), `NZCV = 0x6` (Carry set, Zero set).
3. **Clean Termination:**
   - Syscall `SYS_exit(0)` trapped cleanly by `DarwinBsdSubsystem`.
   - Exit code: 0. Error message: empty.

---

### 3. Forensic Trace Sample (Diagnostic Output)

```csv
step,pc_before,pc_after,instruction_raw,current_el,sp,nzcv,disassembly,register_diffs
0,0x100001000,0x100001004,0xd2800000,0,0x1fffe80,0x0,movz x0, #0,X0: 0x0
1,0x100001004,0x100001008,0xd2800021,0,0x1fffe80,0x0,movz x1, #1,X1: 0x1
2,0x100001008,0x10000100c,0x8b010000,0,0x1fffe80,0x0,add x0, x0, x1,X0: 0x1
3,0x10000100c,0x100001010,0x91000421,0,0x1fffe80,0x0,add x1, x1, #1,X1: 0x2
4,0x100001010,0x100001014,0xf101943f,0,0x1fffe80,0x8,cmp x1, #0x65,NZCV: 0x8
5,0x100001014,0x100001008,0x54ffffa1,0,0x1fffe80,0x8,b.ne 0x100001008,PC: 0x100001008
...
402,0x100001018,0x10000101c,0xd2800000,0,0x1fffe80,0x6,movz x0, #0,X0: 0x0
403,0x10000101c,0x100001020,0xd2800030,0,0x1fffe80,0x6,movz x16, #1,X16: 0x1
404,0x100001020,0x100001024,0xd4001001,0,0x1fffe80,0x6,svc #0x80,EXC: SVC64
```

---

### 4. Conclusion

Actual guest CPU execution of authentic ARM64 instructions at EL0 is conclusively proven. Instructions are retired sequentially with accurate register mutations, loop branch evaluation, and Darwin syscall exception trapping.
