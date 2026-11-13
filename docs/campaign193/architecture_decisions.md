# Campaign 193 — Architecture Decisions

This document formalizes the key architectural decisions and engineering rationales implemented during Campaign 193.

---

## 1. Centralized Architectural Exception Delivery
- **Decision:** Consolidate all CPU exception entry mechanisms into `CPU::deliver_architectural_exception(const ArchitecturalException& exc)`.
- **Rationale:** Prior to Campaign 193, synchronous exception dispatch, interrupt delivery, and userspace syscall handling utilized fragmented paths with differing register preservation logic. Centralizing exception vector calculation, PSTATE saving (`SPSR_EL1`), instruction pointer capture (`ELR_EL1`), syndrome generation (`ESR_EL1`), and fault address recording (`FAR_EL1`) guarantees consistent architectural state across all exception sources.

---

## 2. Syndrome Register ISS 25-Bit Bitmask Correction
- **Decision:** Expand the Instruction Specific Syndrome (ISS) mask in `SynchronousException::esr()` from 24 bits (`0x00FFFFFFu`) to 25 bits (`0x01FFFFFFu`).
- **Rationale:** Section D13.2.39 of ARM DDI 0487 defines the ISS field as bits [24:0] (25 bits total). Bit 24 represents the `ISV` (Instruction Syndrome Valid) flag for Data Aborts. Truncating ISS to 24 bits stripped the `ISV` flag, preventing guest kernels from reliably decoding `SAS` (Access Size) and `SRT` (Syndrome Register Transfer) fields.

---

## 3. Strict 2KB Vector Table Base Alignment Masking
- **Decision:** Mask bits [10:0] of `VBAR_EL1`, `VBAR_EL2`, and `VBAR_EL3` with `~0x7FFULL`.
- **Rationale:** The ARMv8-A architecture mandates that vector tables are aligned on a 2048-byte boundary. Sub-2KB offset bits are RES0. Unmasked assignment previously allowed misaligned bases, producing non-conforming exception vectors.

---

## 4. Banked Stack Pointer (`SP_EL0` vs `SP_EL1`) Architecture
- **Decision:** Maintain distinct physical storage for `SP_EL0` and `SP_EL1`, dynamically swapping `stack_pointer_` according to `current_el_` and `sp_sel_`.
- **Rationale:** An exception entry to EL1 with `SPSel = 1` must immediately direct stack operations to `SP_EL1` without overwriting the user stack pointer in `SP_EL0`. When executing at EL1 with `SPSel = 1`, access to `SP` operates on `SP_EL1`, while access to `SP_EL0` is performed via `MRS/MSR SP_EL0`.

---

## 5. Memory Protection: `SCTLR_EL1.WXN` Enforcement
- **Decision:** Implement W^X (Write XOR Execute) policy via `SCTLR_EL1.WXN` (bit 19).
- **Rationale:** In hardened operating systems (including Darwin / XNU), writable memory regions must never be executable. When `WXN` is set, any page marked writable (`AP[2] == 0`) is architecturally forced to execute-never (`UXN = 1, PXN = 1`), raising Instruction Abort (IFSC `0x0F`) on instruction fetch attempts.

---

## 6. GICv3 System Register CPU Interface
- **Decision:** Implement `ICC_IAR1_EL1`, `ICC_EOIR1_EL1`, `ICC_PMR_EL1`, and `ICC_SGI1R_EL1` directly as ARM64 system instructions.
- **Rationale:** Real ARMv8-A cores with GICv3 rely on system register accesses (`MSR`/`MRS`) rather than legacy GICv2 MMIO CPU interface windows (`GICC_*`). This allows guest kernels to acknowledge interrupts, set priority masks, issue end-of-interrupt, and trigger multi-core SGIs without page-table mapped device MMIO.

---

## 7. Freestanding Zig 0.17.0 Cross-Compilation for Guest Kernel
- **Decision:** Cross-compile freestanding ARM64 guest kernel runtime (`synthetic_el1_kernel`) using Zig 0.17.0 with Clang 22.1.8 / LLVM linker, using Mach-O format with `-pagezero_size 0`.
- **Rationale:** Avoids host C++ mocks standing in for kernel instructions. Produces authentic ARM64 machine instructions verified by `RealArtifactHarness` and loaded by `MachOLoader`. Omitting userspace `__PAGEZERO` ensures the kernel entry point lies within the executable segment.
