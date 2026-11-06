# Stage 37 Implementation Report: MMU Walker Hardening, CPACR_EL1 FP/SIMD Trapping & Darwin System Register Compatibility

**Project:** WinIOS  
**Phase:** 52  
**Stage:** 37  
**Author:** Architectural Conformance & Virtualization Agent  
**Date:** 2026-11-06  
**Status:** PASS  

---

## 1. Executive Summary

Stage 37 hardens the architectural execution foundations of WinIOS in three key areas:
1. **MMU Translation-Table Walker Hardening:** Strict multi-level descriptor validation, Level 1 (1GB) and Level 2 (2MB) block address alignment verification, reserved descriptor bit enforcement (`0b10` rejection), translation table base alignment checks, and page/block permission extraction.
2. **CPACR_EL1 & FP/SIMD Access Control:** First-class registration of `CPACR_EL1` (System Register key `0x4082`, `min_el=1`), addition of `cpacr_el1_` architectural storage to `CPU` and multi-core `SmpCoreState`, decoder-level access control interception in `CPU::step()` across all 6 supported FP/SIMD instruction families, and generation of `ExceptionClass::FpSimdAccessTrap = 0x07` (`ESR_EL1 = 0x1E000000u`) with state preservation upon trap.
3. **Darwin-Relevant System Register Compatibility Research:** Implementation of an isolated, non-executing research registry `DarwinCompatibilityRegistry` in `darwin_sysreg_compat.h/cpp` defining 20 system registers across generic, documented Darwin, and research-only categories with deterministic configuration hashing.

All 96 test suites in both Release and Debug configurations passed 100% (95 passed, 1 skipped for live QEMU launcher).

---

## 2. MMU Walker Hardening Implementation

### 2.1 Descriptor Validation Logic (`mmu.cpp`)
The MMU descriptor validation logic in `MMU::validate_descriptor()` was enhanced:
- **Level 0 (512GB):** Only table descriptors (`0b11`) are permitted. Block descriptors are rejected as illegal level-0 translation faults.
- **Level 1 (1GB):** Validates block descriptors (`0b01`) and table descriptors (`0b11`). For 1GB blocks, output physical address bits [29:12] are verified to be zero (`(desc & 0x3FFFF000ULL) == 0`). Any non-zero bits result in an address size/misalignment fault.
- **Level 2 (2MB):** Validates block descriptors (`0b01`) and table descriptors (`0b11`). For 2MB blocks, output physical address bits [20:12] are verified to be zero (`(desc & 0x001FF000ULL) == 0`).
- **Level 3 (4KB):** Only page descriptors (`0b11`) are permitted. Block descriptors are rejected as translation faults.
- **Reserved Formats:** Any descriptor with type bits `0b10` is strictly rejected as an illegal encoding across all levels.
- **Physical Address Bounds:** Descriptors referencing output physical addresses exceeding the 48-bit physical address limit (`0x0000FFFFFFFFF000ULL`) are rejected.

### 2.2 Permissions and Memory Types
- Descriptors extract User/Privileged access bits:
  - `AP[2:1] = 0b00`: Read/Write EL1, No Access EL0.
  - `AP[2:1] = 0b01`: Read/Write EL1 and EL0.
  - `AP[2:1] = 0b10`: Read-Only EL1, No Access EL0.
  - `AP[2:1] = 0b11`: Read-Only EL1 and EL0.
- Execute-Never extraction: `UXN` (bit 54) and `PXN` (bit 53) are enforced during instruction fetch translation.
- Attribute indexing: `AttrIndx[2:0]` (bits [4:2]) maps into `MAIR_EL1` to classify Normal RAM vs Device-nGnRnE memory.

### 2.3 TLB Hardening
- Implemented TLB invalidate operations:
  - `TLBI VMALLE1`: Invalidates all EL1 stage-1 translations.
  - `TLBI VAE1, Xt`: Invalidates specific virtual address entry.
  - `TLBI ASIDE1, Xt`: Invalidates entries matching specific ASID.
  - `TLBI ALLE1`: Invalidates all stage-1 entries across EL1/EL0.

---

## 3. CPACR_EL1 & FP/SIMD Access Control Implementation

### 3.1 Architectural Register Modeling
- `SysRegId::CPACR_EL1`: Key `0x4082` (`op0=3, op1=0, CRn=1, CRm=0, op2=2`), `min_el=1`, Read/Write.
- Storage: Dedicated `cpacr_el1_` member in `CPU` and `SmpCoreState`.
- Architectural Reset Value: `0x0ULL` (FPEN = 0b00, all FP/SIMD operations trapped).
- Access Control Bits:
  - Bits [21:20] (`FPEN`):
    - `0b00`: Access from EL0 and EL1 causes a trap to EL1.
    - `0b01`: Access from EL0 causes a trap to EL1; access from EL1 does not cause a trap.
    - `0b10`: Access from EL0 and EL1 causes a trap to EL1.
    - `0b11`: Access from EL0 and EL1 does not cause a trap.
- Query API: `cpu.is_fp_simd_enabled(current_el)` evaluates `FPEN` accurately against the executing exception level.

### 3.2 Access-Control Interception Pipeline
Before decoding or mutating architectural register state, `CPU::step()` inspects instruction encodings against supported FP/SIMD patterns:
1. SIMD/FP Load/Store (`(instruction & 0x3E000000u) == 0x3C000000u`)
2. Scalar FP 2-source (`(instruction & 0xFF200C00u) == 0x1E200800u`)
3. Scalar FP 1-source (`(instruction & 0xFF207C00u) == 0x1E204000u`)
4. Scalar FP Compare (`(instruction & 0xFF20FC07u) == 0x1E202000u`)
5. Scalar FP Immediate (`(instruction & 0xFF201FE0u) == 0x1E201000u`)
6. FP <-> Integer / Vector (`(instruction & 0x7F20FC00u) == 0x1E200000u` or `(instruction & 0xBF208C00u) == 0x0E200800u`)

If `!is_fp_simd_enabled(current_el)`:
- When `architectural_exceptions_` is active:
  - Vector target: `VBAR_EL1 + 0x400` (Lower EL using AArch64) or `VBAR_EL1 + 0x200` (Current EL with SP_ELx).
  - Syndrome: `ESR_EL1 = (0x07u << 26) = 0x1E000000u` (ExceptionClass `FpSimdAccessTrap`).
  - `ELR_EL1` set to the trapping instruction address.
  - Step completes without destination register mutation.
- When `architectural_exceptions_` is disabled:
  - Throws `std::runtime_error("Trapped FP/SIMD instruction access via CPACR_EL1.FPEN")`.

---

## 4. Darwin System Register Compatibility Layer

Implemented in `src/cpu/darwin_sysreg_compat.h` and `src/cpu/darwin_sysreg_compat.cpp`:
- **Isolation:** Registry is strictly decoupled from the live CPU execution loop; cannot be executed or triggered by guest instructions without an active compatibility profile.
- **Classification Categories:**
  - `GenericArchitectural`: ARMv8 standard registers (`CPACR_EL1`, `CNTVCT_EL0`, `TTBR0_EL1`, etc.).
  - `PubliclyDocumentedDarwin`: Publicly documented registers from XNU open-source releases (`TPIDR_EL0`, `TPIDRRO_EL0`).
  - `ResearchOnly`: Darwin/Apple Silicon specific research registers (`APSTS_EL1`, `ACC_OVRD_EL1`, `SPRR_CONFIG_EL1`, `GXF_STATUS_EL1`, `M1_PMCR_EL0`).
  - `Unsupported`: Explicitly rejected registers (`ZCR_EL1`, `SVCR`).
- **Profile Management:** `DarwinProfile` configuration generates deterministic CRC32C configuration hashes (`0x71661cb5` for default profile) binding Darwin emulation parameters safely.

---

## 5. Snapshot and Replay Integration

- `cpacr_el1` is serialized within each core's 1080-byte architectural state section in `SmpCoreState`.
- `capture_from_cpu` reads `cpu.get_cpacr_el1()`, and `apply_to_cpu` restores `cpu.set_cpacr_el1()`.
- State hashes include `cpacr_el1` value for multi-core synchronization and deterministic replay validation.
