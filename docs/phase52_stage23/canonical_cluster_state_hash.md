# Canonical Cluster State Hash

## 1. Overview & Mathematical Definition

The Canonical Cluster State Hash provides a deterministic 64-bit fingerprint of the entire architectural execution state across all virtual cores, shared guest memory, and scheduling coordinators.

The hash is calculated using the 64-bit Fowler–Noll–Vo 1a (FNV-1a) non-cryptographic algorithm:
$$\text{Offset Basis} = \text{0xCBF29CE484222325}$$
$$\text{Prime} = \text{0x100000001B3}$$

> [!NOTE]
> FNV-1a is a non-cryptographic hash selected for high evaluation speed ($> 1.5\text{ GB/s}$) and deterministic cross-platform bit consistency. It is not claimed to possess cryptographic collision resistance.

---

## 2. Hash Composition

The hash accumulates architectural components in strict canonical order:

1. **Cluster Topology & Clock**:
   - `registered_core_count` (u64)
   - `global_retirement_sequence` (u64)
2. **Per-Core Architectural States** (iterated in strict ascending `core_id` order):
   - `core_id` (u64)
   - `slot.state.compute_canonical_hash()` (u64)
   - `total_instructions_retired` (u64)
3. **Shared Physical Memory**:
   - Castagnoli CRC32C over the full physical RAM contents:
     `mem_crc = trace::calculate_crc32c(raw_data, size)` (u32)

---

## 3. Inclusion and Exclusion Boundaries

### Included Architectural State
- All 31 general-purpose registers (X0–X30), PC, SP_EL0..3.
- NZCV, DAIF, SP_SEL, IL, PSTATE_SS, CurrentEL.
- All 32 128-bit vector registers (V0–V31), FPCR, FPSR.
- EL1..3 exception syndrome, fault address, return, and base vectors.
- SCTLR, TCR, TTBR0, TTBR1, MAIR, CPACR, CNTVOFF.
- Per-core comparator registers (CNTP_CVAL_EL0, CNTV_CVAL_EL0) and control registers (CNTP_CTL_EL0, CNTV_CTL_EL0).
- Core lifecycle state and retirement counters.
- All guest physical memory bytes.

### Explicitly Excluded Non-Architectural State
- Host pointers (`CPU*`, `Memory*`, `SmpScheduler*`).
- Debug sinks and tracing file descriptors.
- Host thread IDs and OS mutexes.
- Host diagnostic timestamps (`timestamp_ns`).
- Performance telemetry (read/write access counts, throughput meters).
- Dynamic heap buffer allocations.

---

## 4. Invariant Verification

Test Group B (`B2_SmpCoreStateCanonicalHashExact`), Test Group I (`Deterministic Execution Continuation`), and the benchmark suite confirm that serializing, restoring, and executing synthetic multicore workloads across 2, 4, and 8 cores produces 100% bit-exact canonical cluster state hashes across all runs.
