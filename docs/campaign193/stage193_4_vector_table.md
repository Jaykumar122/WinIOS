# CAMPAIGN 193 — STAGE 193.4 VECTOR TABLE ARCHITECTURE

- **Campaign:** 193 (ARM64 Exception Architecture, EL0/EL1 Privilege Transitions & Synthetic Kernel Execution)
- **Stage:** 193.4
- **Baseline Git Commit:** `9ff79b3`
- **Host Platform:** Windows 11 x64 (MSVC C++20, `/W4 /WX /permissive-`)
- **Target Device Identity:** iPhone 15 (`iPhone15,4`, `D37AP`, Apple A16 `T8120`)
- **Status:** **COMPLETED — VERIFIED**

---

## 1. OBJECTIVE & ARCHITECTURAL REQUIREMENTS

Stage 193.4 implements and validates ARM64 exception vector table selection and dispatch using `VBAR_EL1`:
1. **Vector Alignment & Masking:**
   - Under ARMv8-A D13.2.140, `VBAR_EL1` (and EL2/EL3 counterparts) requires 2KB alignment (bits [10:0] are RES0).
   - Writes to `VBAR_EL1` via guest `MSR` instruction or C++ API must mask bits [10:0] with `~0x7FFULL`.
2. **Vector Offset Selection:**
   - ARMv8-A defines four 0x200-byte table sections:
     - Section 0 (+0x000): Current EL with SP0
     - Section 1 (+0x200): Current EL with SPx
     - Section 2 (+0x400): Lower EL using AArch64
     - Section 3 (+0x600): Lower EL using AArch32
   - Within each section, four 0x80-byte offsets correspond to:
     - +0x000: Synchronous
     - +0x080: IRQ / vIRQ
     - +0x100: FIQ / vFIQ
     - +0x180: SError / vSError
3. **Execution State Support & Rejection:**
   - Apple A16 / iPhone 15 silicon does not support AArch32 execution at any EL. WinIOS explicitly exposes `supports_aarch32_lower_el() == false`.
4. **Guest Architectural Vector Dispatch:**
   - Real guest instructions that trigger exceptions must cause the guest CPU to jump directly to the computed `VBAR_EL1 + offset`.
   - Handler instructions residing in guest memory at the vector address must execute on subsequent `CPU::step()` cycles.

---

## 2. IMPLEMENTATION & DEFECT REMEDIATION

### Defect Identified: VBAR_ELx RES0 Bits [10:0]
- In `src/cpu/cpu.cpp` lines 8968, 9109, and 9151, `MSR VBAR_ELx` previously assigned `vbar_elx_ = val` without masking bits [10:0].
- Remediation: Updated all three MSR handlers to enforce `vbar_elx_ = val & ~0x7FFULL`.
- Updated `set_vbar_el2` and `set_vbar_el3` in `src/cpu/cpu.h` to enforce `& ~0x7FFULL` identically to `set_vbar_el1`.

### AArch32 Vector Offset & Explicit Rejection
- Added explicit support for the Lower EL AArch32 section (+0x600) in `compute_vector_offset` while adding `CPU::supports_aarch32_lower_el() const noexcept { return false; }` to formally document and test that AArch32 lower EL execution is not supported on this device identity.

---

## 3. VERIFICATION & TEST RESULTS

Unit test suite `tests/unit/stage193_4_vector_table_tests.cpp` was implemented and registered in `CMakeLists.txt`.

### Test Cases:
1. `test_vbar_alignment_masking`:
   - Validates direct API and guest `MSR VBAR_EL1, X0` masking of bits [10:0].
   - Verified that `0xFFFF000012345678ULL` becomes `0xFFFF000012345000ULL` on readback via `MRS X1, VBAR_EL1`.
2. `test_vector_offset_computation_all_groups`:
   - Validates all 16 vector offsets across Current EL SP0, Current EL SPx, Lower EL AArch64, and Lower EL AArch32.
   - Validates `supports_aarch32_lower_el() == false`.
3. `test_guest_instruction_exception_dispatch_lower_el`:
   - Guest at EL0 executes `SVC #0` at `0x4000`.
   - Exception enters EL1 with SP_EL1, sets `ELR_EL1 = 0x4004`, and jumps PC to `VBAR_EL1 + 0x400`.
   - Guest instruction `MOV X5, #100` placed at `VBAR_EL1 + 0x400` executes on next step, setting `X5 = 100`.
4. `test_guest_instruction_exception_dispatch_same_el_spx`:
   - Guest at EL1 with SP_EL1 executes unallocated instruction `0x00000000u` at `0x10000`.
   - Synchronous exception jumps PC to `VBAR_EL1 + 0x200` (Current EL SPx Sync).
   - Guest instruction `MOV X6, #200` placed at `VBAR_EL1 + 0x200` executes, setting `X6 = 200`.
5. `test_guest_instruction_exception_dispatch_same_el_sp0`:
   - Guest at EL1 with SP0 (`SPSel = 0`) executes unallocated instruction `0x00000000u` at `0x10000`.
   - Synchronous exception jumps PC to `VBAR_EL1 + 0x000` (Current EL SP0 Sync).
   - `SPSel` is set to 1, and guest instruction `MOV X7, #300` executes, setting `X7 = 300`.
6. `test_guest_asynchronous_vector_dispatch`:
   - Validates IRQ and FIQ vector dispatch from Lower EL (+0x480, +0x500), Current EL SPx (+0x280, +0x300), and Current EL SP0 (+0x080, +0x100).

### Execution Verification:
- **Release:** 6/6 passed.
- **Debug:** Verified via task-53130.
