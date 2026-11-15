# WinIOS Campaign 199: Snapshot Serialization & Replay Report

**Subsystem:** State Serialization, Virtual Machine Snapshot & Replay  
**Target Profile:** Apple A16 Bionic `T8120` Virtual Platform  
**Classification:** `SNAPSHOT_REPLAY`  
**Status:** FULLY IMPLEMENTED & VERIFIED  

---

## 1. Architectural Scope

Deterministic virtualization and fault reproduction require comprehensive system snapshot and restore capabilities. In Campaign 199 Stage 17, snapshot serialization and deserialization was extended across all newly introduced virtual platform components:

1. **Stage-2 Translation Subsystem (`Stage2TranslationEngine`):**
   - Binary serialization of virtualization enable state (`HCR_EL2.VM`).
   - Configuration registers (`VTCR_EL2`, `VTTBR_EL2`), translation granule, and active VMID.
   - Stage-2 page table entries and TLB cache states.
2. **Virtual IOMMU & Device Bindings (`VirtualIommu`):**
   - Global IOMMU enable flags and domain count.
   - Per-domain translation tables (IOVA -> PA mappings) and access control attributes.
   - Device attachment table mapping `DeviceId` to `IommuDomainId`.
   - Thread-safe serialization under `std::recursive_mutex` avoiding deadlocks during in-place restoration.
3. **ARM64e Pointer Authentication Keyring (`Arm64ePointerAuth`):**
   - Cryptographic state preservation: 5 128-bit architectural keys (`APIAKey`, `APIBKey`, `APDAKey`, `APDBKey`, `APGAKey`).
   - Fault delivery mode configuration (`PacFaultMode`).
   - Syndrome history and last fault metadata.
4. **End-to-End System Integration:**
   - Full virtual platform snapshot capture saving CPU registers, system registers (`HCR_EL2`, `VTCR_EL2`, `VTTBR_EL2`, `APIAKeyLo/Hi_EL1`, etc.), physical memory, Stage-2 translation state, and IOMMU domain configurations into a self-contained binary payload.
   - Complete restore and resume verification with bit-for-bit fidelity.

---

## 2. Verification Evidence

Validated through `stage199_17_snapshot_iommu_pac_tests`:
- `test_stage2_mmu_snapshot_roundtrip`: Serialized active Stage-2 translation table with 16KB mappings; restored into clean instance; verified translation fidelity and attribute preservation.
- `test_virtual_iommu_snapshot_roundtrip`: Serialized IOMMU with multiple domains and attached devices; restored into clean instance; verified IOVA translation and device isolation.
- `test_pac_keyring_snapshot_roundtrip`: Serialized custom 128-bit PAC keyring; restored; verified exact pointer authentication and tag calculation equivalence.
- `test_full_system_snapshot_with_stage2_sysregs`: Captured full platform snapshot during execution; altered state; restored from snapshot; verified full restoration of CPU registers, system registers, and memory.
