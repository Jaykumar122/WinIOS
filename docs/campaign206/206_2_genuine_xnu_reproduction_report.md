# Stage 206.2: Reproduce Genuine XNU Entry Report

## 1. Executive Summary
- **Stage:** 206.2 — Reproduce Genuine XNU Entry
- **Status:** PASS
- **Date:** 2026-11-16
- **Test Executable:** `build/Release/stage205_18_xnu_execution_attempt_tests.exe`
- **Result:** Exact reproduction confirmed with bit-exact hash match, identical instruction count (19 retired), and identical faulting address (`0x23B100000`).

---

## 2. Artifact Provenance & Identification
- **Artifact Category:** Target B — Public-Source Darwin/XNU Bootstrap Candidate
- **Source Material:** Canonical Apple Open Source Darwin/XNU `osfmk/arm64/start.s` early prologue
- **Format:** 64-bit ARM64 Mach-O Executable (`MH_EXECUTE`, `CPU_TYPE_ARM64`)
- **Recorded SHA-256 Digest:** `3d2d8e2b0a107cd85b0a22186f13c75062569c14a1d129f95ac13cc71d0afd8e`
- **Computed SHA-256 Digest:** `3d2d8e2b0a107cd85b0a22186f13c75062569c14a1d129f95ac13cc71d0afd8e` (Exact match)
- **Image Size:** 65,536 bytes (64 KB)

---

## 3. Execution Verification Data
- **Entry Virtual Address:** `0x0000000000001000`
- **First Instruction:** `0xD5034FDF` (`msr daifset, #0xf` — mask FIQ, IRQ, SError, and Debug exceptions)
- **Total Instructions Retired Before Blocker:** 19 instructions
- **Execution Privilege Level:** EL1h (SP_EL1 selected)
- **Preserved Boot Arguments Pointer:** `X0 = 0x20000`, `X19 = 0x20000`
- **UART Output Observed:** Bytes `'X'`, `'N'`, `'U'` written to UART TX buffer at `0x90000`
- **Terminal Exception Address:** PC = `0x0000000000001048`
- **Exception Class:** Synchronous Data Abort (`ESR_EL1.EC = 0x25`)
- **Faulting Address:** `FAR_EL1 = 0x000000023B100000`
