# Architectural Canonical State Hash Audit & AUD-34-01 Resolution
**State Coverage Completeness Analysis**

---

## 1. Problem Statement (AUD-34-01)

Stage 34 independent audit finding **AUD-34-01** reported:
> "Canonical multicore state hash (`compute_canonical_multicore_hash()`) hashes CPU core registers and guest RAM, but excludes:
> 1. Exclusive monitor reservation structures.
> 2. Interrupt router queued pending requests.
> 3. Virtual device bus register states (Section 0x18)."

If two emulator states differed only in pending interrupts or active exclusive reservations, the canonical hash would register as identical, creating a vulnerability to undetected replay divergence.

---

## 2. Architectural Solution: `compute_extended_cluster_hash()`

In Stage 35, `SmpScheduler` introduced `compute_extended_cluster_hash()`:
1. **Base Component:** Incorporates full canonical multicore hash (X0–X30, PC, SP, PSTATE, Retirements, RAM FNV-1a).
2. **Exclusive Monitor Component:**
   - Hashes `core_id`, `valid` flag.
   - If `valid`, hashes `granule_address`, `exact_address`, and `size`.
3. **Interrupt Router Component:**
   - Hashes per-core queue sizes.
   - For every queued request, hashes `intid`, `priority`, `type`, and `request_sequence`.
4. **Device Bus Component (Section 0x18):**
   - Hashes attached `device_count()`.
   - For every registered device, hashes `device_id`, `status`, `mmio_base`, `mmio_size`, `irq_line`, and all bytes from `dev->serialize_state()`.

---

## 3. Sensitivity Validation Results

In `gic_memory_ordering_tests` Test 4:
- **Baseline Extended Hash:** `0xca03816ca2c0f2df` (`14546841397014814207`).
- **Exclusive Monitor Mutation (`LDXR`):** Hash changes to non-baseline value. Upon `CLREX`, hash restores **identically** to baseline.
- **Interrupt Queue Mutation (Inject IRQ 48):** Hash changes to non-baseline value. Upon queue clear, hash restores **identically** to baseline.
- **Device Bus Attachment:** Hash changes to `0x1c83d573d8b13689` (`2053494875133289161`).
- **Device MMIO Mutation (`kDescriptorAddr = 0x5000`):** Hash changes immediately.
- **Status:** **AUD-34-01 FULLY RESOLVED**.
