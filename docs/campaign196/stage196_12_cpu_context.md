# Stage 196.12 — Complete CPU Context Preservation (Acceptance Test I)

## 1. Architectural Implementation
- **Architectural Exception Frame Layout:**
  - `ExceptionFrame` represents the 896-byte hardware exception and preemption context saved on kernel stack or thread switch.
  - Compile-time layout verification ensures exact 8-byte and 16-byte alignment across all members:
    - `0x000`: Magic (`0x4354585446524D31ULL`, "CTXTFRM1")
    - `0x008`: Frame size (`0x380`, 896 bytes)
    - `0x010`: ELR_EL1 (return address / PC)
    - `0x018`: SPSR_EL1 (saved PSTATE including NZCV and DAIF)
    - `0x020`: SP_EL0
    - `0x028`: SP_EL1
    - `0x030`: TPIDR_EL0 (Thread Local Storage pointer user)
    - `0x038`: TPIDR_EL1 (Thread Local Storage pointer kernel)
    - `0x040`: ASID
    - `0x048`: TTBR0_EL1
    - `0x050`: FAR_EL1 (Fault Address Register)
    - `0x058`: ESR_EL1 (Exception Syndrome Register)
    - `0x060`: FPCR
    - `0x064`: FPSR
    - `0x068`: exception_origin_el
    - `0x06C`: simd_saved
    - `0x070`: General purpose registers X0..X30 (31 * 8 = 248 bytes, ending at 0x168)
    - `0x168`: 8-byte pad0 (aligning to 16 bytes for NEON)
    - `0x170`: SIMD/Vector registers Q0..Q31 (32 * 16 = 512 bytes, ending at 0x370)
    - `0x370`: 16-byte reserved (padding to 896 bytes)
- **State Capture & Restore Fidelity:**
  - `ArchitecturalCpuContext::capture` saves all 31 GPRs, PC, SP_EL0, SP_EL1, NZCV, DAIF, SPSR_EL1, TPIDR_EL0, TPIDR_EL1, TTBR0_EL1, ASID, CPACR_EL1, FPCR, FPSR, and all 32 128-bit SIMD registers Q0..Q31.
  - `ArchitecturalCpuContext::restore` restores all state bit-for-bit into the target `CPU` core, including setting `ELR_EL1` to `pc`.
  - Serialization to memory via `write_frame_to_memory` and deserialization via `read_frame_from_memory` validates round-trip lossless preservation across memory serialization.

## 2. Test Evidence (Acceptance Test I)
Verified via `stage196_11_tlb_and_context_tests`:
- Static assertions on all 20 structure offset invariants.
- Test I1: Round-trip capture from `CPU1` into `ArchitecturalCpuContext`, memory serialization, deserialization into context 2, equality assertion, and full restoration into fresh `CPU2`. All 31 GPRs, PC, SP_EL0, SP_EL1, ELR_EL1, SPSR_EL1, DAIF, NZCV, TPIDR_EL0, TPIDR_EL1, FPCR, FPSR, and 32 Q registers match exactly.

## 3. Evidence Classification
`GUEST_SMP`, `GUEST_EL1`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
