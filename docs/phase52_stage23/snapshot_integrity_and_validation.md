# Snapshot Integrity & Validation Specification

## 1. Multi-Stage Validation Architecture

The cluster snapshot validation engine (`validate_smp_snapshot_buffer` in `src/snapshot/smp_snapshot_validation.cpp`) enforces strict syntactic, structural, and semantic constraints on all `.wsnap` v2 artifacts before any restoration or processing can take place.

---

## 2. Validation Stages

### Stage 1: Container & Header Validation
- Buffer size $\ge 64$ bytes (`kWSnapHeaderSize`).
- Magic bytes match `kWSnapMagic` (`'W','S','N','A','P','\0','\1','\0'`).
- Header CRC32C over bytes 0..59 matches stored CRC at byte 60.
- Version equals `kWSnapVersion2` (2). Any other version rejected with `UnsupportedVersion`.
- Core count $N \in [1, 128]$.
- Memory capacity $> 0$.

### Stage 2: Section Directory & Boundary Validation
- Section directory offset $\le \text{file\_size}$.
- Section count $\le 1024$.
- Directory bytes ($N_{\text{sec}} \times 40$) fit within file boundary.
- For every section descriptor:
  - `file_offset` and `compressed_size` do not overflow and fit within file size.
  - No two sections overlap in byte ranges.
  - Section payload CRC32C matches stored descriptor CRC32C.

### Stage 3: Mandatory Cluster Section Inventory
Verifies the presence of all mandatory cluster sections:
- `CORE_DIRECTORY` (`0x11`)
- `SHARED_MEMORY` (`0x13`)
- `SCHEDULER` (`0x14`)
- `EXCLUSIVE_MONITOR` (`0x15`)
- `INTERRUPT_ROUTER` (`0x16`)
- `INTEGRITY` (`0x17`)
- `CORE_ARCHITECTURAL_STATE` (`0x12`) for every core $c \in [0, \text{core\_count}-1]$.

### Stage 4: Semantic & Relational Integrity
- **Core Directory**: Entry count matches `header.core_count`.
- **Shared Memory**: Total memory size matches `header.memory_capacity`. Chunk indices $< \text{total\_chunks}$.
- **Scheduler**: `registered_core_count` matches `header.core_count`. Current core index $< \text{registered\_core\_count}$.
- **Exclusive Monitor**: Target core references $< \text{max\_cores}$. Granule addresses 64-byte aligned.
- **Interrupt Router**: Target core references $< \text{max\_cores}$.
- **Integrity**: Section CRC table in `Integrity` matches actual section CRCs computed in descriptor table.

---

## 3. Adversarial Test Verification

Test Group G (`test_group_g_adversarial_validation`) systematically tests:
- `G1_ValidBufferPasses`: Valid snapshot passes all 4 stages.
- `G2_TruncatedBufferRejected`: Buffer truncated to 40 bytes rejected with `TruncatedFile`.
- `G3_HeaderCrcCorruptionRejected`: Bit flip in header rejected with `HeaderCrcMismatch`.
- `G4_SectionPayloadCrcCorruptionRejected`: Bit flip in section payload rejected with `SectionCrcMismatch`.
- `G5_UnsupportedVersionRejected`: Version set to 99 rejected with `UnsupportedVersion`.
