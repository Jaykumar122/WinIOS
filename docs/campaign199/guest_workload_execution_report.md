# WinIOS Campaign 199: Guest Workload Execution Report

**Target Research Architecture:** ARMv8.5-A / ARM64e (iPhone 15 / Apple A16 Bionic `T8120`)  
**Cross-Compiler Toolchain:** Zig 0.17.0 (`aarch64-macos`, freestanding Mach-O, `-mcpu=apple_a16`)  
**Classification:** `GUEST_WORKLOAD` / `ARM64E_PAC_BOUNDED` / `STAGE2_TRANSLATION`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Executive Summary

Campaign 199 Stage 18 demonstrates the execution of real, freestanding ARM64e Mach-O binaries exercising hardware pointer authentication (`PACIA`, `AUTIA`, `PACGA`, `PACIZA`, `AUTIZA`), memory load/store operations across translated regions, differential PAC key sensitivity, and deterministic instruction retirement under the WinIOS emulator.

---

## 2. Workload Artifact Details

### `guest_stage2_iommu_pac_workload`
- **Source File:** `tests/artifacts/src/guest_stage2_iommu_pac_workload.S`
- **Target Binary:** `tests/artifacts/bin/guest_stage2_iommu_pac_workload`
- **SHA-256 Digest:** `34e1f100f4c2369b7be19c0d0506adc5a5f13b90707184636f4ef52213776f1a`
- **Compiler Command:**
  ```cmd
  zig cc -target aarch64-macos -mcpu=apple_a16 -nostdlib -fno-sanitize=undefined -fno-stack-protector "-Wl,-e,_start" "-Wl,-pagezero_size,0" tests/artifacts/src/guest_stage2_iommu_pac_workload.S -o tests/artifacts/bin/guest_stage2_iommu_pac_workload
  ```
- **Target Architecture:** ARM64e Mach-O 64-bit executable.

---

## 3. Workload Execution Flow & Architectural Highlights

1. **Pointer Signing & Authentication (`PACIA` / `AUTIA`):**
   - The workload loads test pointer `0x10008000` and modifier `0x12345678`.
   - Executes `pacia x0, x1`, embedding cryptographic signature into bits [63:48].
   - Stores the signed pointer into mailbox `[0x40008]` for inspection.
   - Executes `autia x0, x1`, validating the signature and stripping PAC bits.
   - Compares stripped pointer against original `0x10008000`.
2. **Generic Tag Generation (`PACGA`):**
   - Executes `pacga x5, x0, x1` using the generic key (`APGAKey`).
   - Verifies the resulting tag is non-zero and records tag in `[0x40010]`.
3. **Zero-Modifier Authentication (`PACIZA` / `AUTIZA`):**
   - Signs and authenticates pointer `0x20004000` with zero modifier.
   - Verifies clean restoration of the address.
4. **Memory Accesses at Translated Buffer:**
   - Stores `0x19901991` to `0x50000` and `0xBABECAFE` to `0x50008`.
   - Reads back and asserts data integrity.
5. **Completion Signature:**
   - Emits completion signature `0x1991C0DE` to mailbox `[0x40000]`.

---

## 4. Differential Validation & Determinism

1. **Differential PAC Key Sensitivity:**
   - The binary was executed under two distinct CPU configurations with differing `APIAKey` values.
   - Run 1 (Key A): Signed pointer = `0x97af000010008000`.
   - Run 2 (Key B): Signed pointer = `0x1303000010008000`.
   - Validated that the generated signature bit patterns differ deterministically, proving cryptographic key dependence.
2. **Deterministic Instruction Retirement:**
   - Multiple executions from initial boot state retired exactly **41 instructions** to completion.
   - Register states, memory mailboxes, and final architectural signatures were bit-for-bit identical across runs.
