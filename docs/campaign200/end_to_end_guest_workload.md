# WinIOS Campaign 200: End-to-End Guest Workload Execution Report

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) Virtual Platform  
**Stage:** 200.23  
**Status:** VALIDATED  
**Evidence Classification:** `GUEST_WORKLOAD` & `ARM64E_PAC_BOUNDED`  

---

## 1. Executive Summary

This report validates the end-to-end execution of a genuine ARM64e Mach-O guest application (`guest_stage200_end_to_end_workload`) within the WinIOS virtual execution environment. The binary was cross-compiled from source assembly using the Zig 0.17.0 toolchain targeting `aarch64-macos` with `-mcpu=apple_a16`.

The guest application exercises:
1. **ARM64e Pointer Authentication (PAC):**
   - Signing and authenticating pointers via `pacia` and `autia`.
   - Generating generic cryptographic message authentication codes via `pacga`.
   - Zero-modifier signing and verification via `paciza` and `autiza`.
2. **Virtual Memory Workspace Interaction:**
   - Multi-word memory writes and reads inside 16KB-granule page translation spaces.
   - Mailbox communication signaling completion with architectural token `0x2000C0DE`.
3. **Differential Cryptographic Key Sensitivity:**
   - Execution across distinct cryptographic key configurations confirms that pointer signatures vary deterministically according to active `APIA` and `APGA` keyrings.

---

## 2. Test Verification Matrix

| Stage | Test Suite | Scope & Invariants Verified | Status |
|---|---|---|---|
| **200.20** | `stage200_20_complete_snapshot_tests` | Complete platform state serialization (CPUs, PAC, Stage-2 MMU, IOMMU). | PASS |
| **200.21** | `stage200_21_snapshot_recovery_tests` | State restoration fidelity, translation post-restore, corrupted payload rejection. | PASS |
| **200.22** | `stage200_22_deterministic_replay_tests` | Checkpoint-restart bit-exact execution replay across multi-step workloads. | PASS |
| **200.23** | `stage200_23_guest_workload_tests` | Execution of genuine compiled ARM64e Mach-O binary, PAC verification, and differential key sensitivity. | PASS |

---

## 3. Workload Execution Trace & Output Verification

Upon loading the 50,176-byte Mach-O binary at physical base `0x0`, the CPU executes from `_start` at EL1h:
- Pointer `0x10008000` is signed with modifier `0x12345678` using `pacia x0, x1` $\rightarrow$ stored at mailbox offset `0x08`.
- Pointer is verified and restored via `autia x0, x1` $\rightarrow$ matches `0x10008000`.
- Generic tag generated via `pacga x5, x0, x1` $\rightarrow$ non-zero 64-bit tag stored at mailbox offset `0x10`.
- Zero-modifier pointer `0x20004000` is signed and restored via `paciza` / `autiza` $\rightarrow$ matches `0x20004000`.
- Memory words `0x20002000` and `0xCAFEBABE` written and read back from workspace buffer `0x50000`.
- Successful completion signature `0x2000C0DE` written to mailbox base `0x40000`.

---

## 4. Conclusion

Stage 200.23 confirms genuine guest execution of independently compiled ARM64e binaries on the WinIOS virtual platform, validating the full hardware emulation pipeline from Mach-O loading to instruction retirement.
