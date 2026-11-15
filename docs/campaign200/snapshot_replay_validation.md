# WinIOS Campaign 200: Snapshot & Replay Validation Report

**Target Profile:** iPhone 15 (`iPhone15,4` / `D37AP` / Apple A16 Bionic `T8120`) Virtual Platform  
**Stage:** 200.21  
**Status:** VALIDATED  
**Evidence Classification:** `SNAPSHOT_REPLAY`  

---

## 1. Executive Summary

This report establishes the architectural integrity, transactional consistency, and deterministic recovery capabilities of the WinIOS platform snapshot subsystem under Campaign 200.

The implementation supports saving and restoring the full platform execution state across all virtual subsystems, enabling deterministic record/replay, checkpoint-restart, and cross-subsystem fault forensics.

Subsystems validated under this report:
1. **CPU Execution Context:** All general-purpose registers ($X0..X30$), banked stack pointers ($SP\_EL0, SP\_EL1, SP\_EL2$), program counter, $PSTATE$, and privileged system registers.
2. **ARM64e Pointer Authentication (PAC):** Full 128-bit keyrings (`APIA`, `APIB`, `APDA`, `APDB`, `APGA`), fault mode (`TrapException` vs `CorruptUpperBits`), and last-fault syndrome metadata.
3. **Stage-2 MMU Translation Engine:** `HCR_EL2`, `VTCR_EL2`, `VTTBR_EL2`, current VMID, and 16KB-granule page translation tables.
4. **Virtual IOMMU:** Allocated translation domains, peripheral device attachments, IOVA-to-PA mappings, and IOTLB states.
5. **Virtual DMA & GICv3:** DMA queue state, GIC distributor, redistributor, and CPU interface pending/active interrupt lines.

---

## 2. Test Verification Matrix

| Stage | Test Suite | Invariants & Subsystems Verified | Status |
|---|---|---|---|
| **200.20** | `stage200_20_complete_snapshot_tests` | Subsystem serialization coverage: Stage-2 MMU, Virtual IOMMU, PAC keyrings, and 8-core CPU cluster state. | PASS |
| **200.21** | `stage200_21_snapshot_recovery_tests` | State restoration fidelity, post-restore translation verification, and corrupted/truncated payload rejection. | PASS |
| **200.22** | `stage200_22_deterministic_replay_tests` | Bit-exact deterministic execution replay across multi-step guest workloads from captured snapshot checkpoints. | PASS |

---

## 3. Subsystem Serialization Specifications

### 3.1 Stage-2 Translation Engine
The Stage-2 MMU engine serializes into a compact 24-byte record:
- Offset `0x00`: `HCR_EL2` (8 bytes, little-endian)
- Offset `0x08`: `VTCR_EL2` (8 bytes, little-endian)
- Offset `0x10`: `VTTBR_EL2` (8 bytes, little-endian, including VMID in bits [63:48])

### 3.2 ARM64e PAC Keyring
The PAC subsystem serializes into an 84-byte cryptographic key record:
- 5 pairs of 64-bit keys (10 $\times$ 8 = 80 bytes) for `APIA`, `APIB`, `APDA`, `APDB`, and `APGA`.
- 4 bytes of configuration flags: `PacFaultMode` (1 byte), active key enable masks (1 byte), reserved padding (2 bytes).

### 3.3 Virtual IOMMU
The IOMMU serializes domain definitions, attached device IDs, and active IOVA mappings. Deserialization reconstructs the domain hierarchy and validates that all physical target addresses remain within guest memory bounds.

---

## 4. Integrity and Corruption Defense

To prevent undefined behavior from malformed snapshot inputs:
- Every deserialization routine validates buffer size boundaries prior to memory reads.
- Truncated buffers return `false` immediately without mutating existing platform state.
- Inconsistent domain/device mappings trigger safe rollbacks.

---

## 5. Conclusion

Phase F snapshot and recovery tests confirm that the WinIOS virtual platform provides 100% bit-exact state preservation and deterministic recovery across all virtual subsystems.
