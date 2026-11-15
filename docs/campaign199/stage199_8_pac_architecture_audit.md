# WinIOS Campaign 199 — Stage 199.8 Pointer Authentication Architecture Audit

**Campaign:** 199  
**Subsystem:** ARM64e Pointer Authentication (PAC)  
**Date:** 2026-11-15  
**Audit Target:** `src/cpu/pointer_auth.h`, `src/cpu/pointer_auth.cpp`, `src/cpu/cpu.h`, `src/cpu/cpu.cpp`, `src/cpu/sysreg.cpp`  

---

## 1. Architectural Scope & Current State

WinIOS targets Apple A16 Bionic (`iPhone15,4` / `D37AP` / `T8120`), which implements ARMv8.6-A Pointer Authentication with Faulting Pointer Authentication Code (FPAC). Under FPAC, attempting to authenticate an invalid pointer immediately triggers a synchronous exception (EC `0x1C`) rather than corrupting upper pointer bits as in pre-ARMv8.6 PAC.

### Current Implementation in `src/cpu/pointer_auth.h` and `pointer_auth.cpp`:
- **Key Representation:** `PacKey128` (128-bit key per type: `APIA`, `APIB`, `APDA`, `APDB`, `APGA`).
- **Key Ring Structure:** `PacKeyRing` manages the complete 5-key set.
- **Fault Modes:** `PacFaultMode::TrapException` (FPAC default, EC `0x1C`) and `PacFaultMode::CorruptUpperBits` (legacy mode).
- **Core Instruction Primitives:**
  - `pacia`, `pacib`, `pacda`, `pacdb`
  - `paciza`, `pacizb`, `pacdza`, `pacdzb`
  - `autia`, `autib`, `autda`, `autdb`
  - `autiza`, `autizb`, `autdza`, `autdzb`
  - `xpaci`, `xpacd`
  - `braa`, `brab`, `blraa`, `blrab`, `retaa`, `retab`

---

## 2. Gaps Identified

1. **CPU Integration:**
   - The `CPU` class does not yet instantiate `Arm64ePointerAuth` as a core component.
   - Core registers for PAC keys (`APIAKeyLo_EL1`, `APIAKeyHi_EL1`, `APIBKeyLo_EL1`, etc.) are not yet connected to CPU MSR/MRS decoding with EL1 privilege checks.
2. **Instruction Decoding in `cpu.cpp`:**
   - The main CPU instruction execution loop in `cpu.cpp` lacks opcode decoders for data processing (1 source / 2 source) PAC instructions and branches (`BRAA`, `BRAB`, `BLRAA`, `BLRAB`, `RETAA`, `RETAB`).
3. **Cryptographic Fidelity Classification:**
   - The PAC tag generation currently utilizes a 3-round SipHash-based 16-bit compression function (`compute_pac_tag`).
   - Real Apple Silicon hardware utilizes a proprietary implementation of QARMA-64 or PRINCE-like tweakable block ciphers with hardware-fused key schedules.
   - **Architectural vs. Cryptographic Classification:** The WinIOS PAC implementation is formally classified as an **architectural emulation with bounded algorithmic hashing** (`ARM64E_PAC_BOUNDED`). It accurately models all instruction semantics, register access privileges, modifier interactions, and FPAC trap exceptions, while using an efficient software mixer rather than hardware-equivalent cryptography.

---

## 3. Implementation Plan for Stages 199.9 – 199.11

1. **Stage 199.9:** Expose architectural PAC key registers in `CPU` and `sysreg.cpp` with EL1-only privilege protection and reset values.
2. **Stage 199.10:** Integrate PAC instruction execution into `CPU::step` in `src/cpu/cpu.cpp` for all standard PAC instructions (`PACIA`, `PACIB`, `AUTIA`, `AUTIB`, `PACDA`, `PACDB`, `AUTDA`, `AUTDB`, `XPAC`, `PACGA`).
3. **Stage 199.11:** Validate deterministic FPAC trap exception generation on modified/corrupted pointers and verify EL0 vs EL1 privilege boundaries.
