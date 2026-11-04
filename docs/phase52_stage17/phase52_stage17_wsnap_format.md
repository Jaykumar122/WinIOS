# WinIOS Phase 52, Stage 17: Binary Snapshot File Format (.wsnap) Specification

## 1. Specification Overview

The `.wsnap` file format is the official binary storage format for WinIOS architectural snapshots. It is engineered for:
- Fast, bit-exact capture and deserialization of architectural execution state.
- Strong cryptographic integrity guarantees via CRC32C (Castagnoli $0x1EDC6F41$).
- High extensibility via typed, self-describing section headers.
- Total decoupling from dynamic execution trace formats (`.wtr`).

All fields in `.wsnap` are encoded strictly in **Little-Endian** byte order.

---

## 2. Binary File Layout

A `.wsnap` snapshot file consists of three contiguous areas:
1. **WSnap File Header (Fixed 64 bytes)**
2. **Section Directory (Array of $N \times 40$-byte Section Descriptors)**
3. **Payload Storage (Variable-length contiguous section data payloads)**

```
+-------------------------------------------------------------------------+
|                        WSNAP FILE HEADER (64 Bytes)                     |
| Magic (8B) | Version (4B) | Cores (4B) | MemSize (8B) | SecCount (4B) ..|
+-------------------------------------------------------------------------+
|                     SECTION DIRECTORY (N x 40 Bytes)                    |
| [Section 0: CPU_STATE]     Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 1: PMU_STATE]     Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 2: DEBUG_STATE]   Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 3: MEMORY_STATE]  Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 4: TIMER_STATE]   Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 5: GIC_STATE]     Type, CoreID, Offset, Size, UncompSize, CRC32|
| [Section 6: METADATA]      Type, CoreID, Offset, Size, UncompSize, CRC32|
+-------------------------------------------------------------------------+
|                         SECTION DATA PAYLOADS                           |
| Payload 0: CPU Architectural State (1128 Bytes)                         |
| Payload 1: PMU State (96 Bytes)                                         |
| Payload 2: Debug Architecture State (144 Bytes)                         |
| Payload 3: Sparse Guest Memory Chunks (Variable)                        |
| Payload 4: Virtual Time & Timers State (88 Bytes)                       |
| Payload 5: GICv3 Distributor & Redistributor State (1212 Bytes)         |
| Payload 6: Snapshot Metadata (Variable)                                 |
+-------------------------------------------------------------------------+
```

---

## 3. File Header Schema (Fixed 64 Bytes)

The header starts at file offset `0x00000000` and occupies exactly 64 bytes:

| Byte Offset | Field Name | Type | Size | Description |
|---|---|---|---|---|
| `0x00` | `magic` | `uint64_t` | 8 bytes | Magic signature: ASCII `"WINIOSNP"` (`0x504E534F494E4957ULL`). |
| `0x08` | `format_version` | `uint32_t` | 4 bytes | Format version number. Current value: `1`. |
| `0x0C` | `core_count` | `uint32_t` | 4 bytes | Total number of CPU cores captured in this snapshot. |
| `0x10` | `memory_capacity` | `uint64_t` | 8 bytes | Configured guest physical memory capacity in bytes. |
| `0x18` | `timestamp_ns` | `uint64_t` | 8 bytes | Host wall-clock timestamp at snapshot creation (nanoseconds since epoch). |
| `0x20` | `virtual_time_ticks` | `uint64_t` | 8 bytes | Emulated system counter tick count (`cntpct_el0`). |
| `0x28` | `section_count` | `uint32_t` | 4 bytes | Number of section descriptors in the directory table ($N$). |
| `0x2C` | `flags` | `uint32_t` | 4 bytes | Snapshot feature flags (bit 0: compressed, bits 1-31: reserved). |
| `0x30` | `reserved` | `uint8_t[12]`| 12 bytes | Reserved for forward compatibility. Must be zero-filled. |
| `0x3C` | `header_crc32c` | `uint32_t` | 4 bytes | CRC32C calculated over bytes `0x00` through `0x3B` (first 60 bytes). |

### Header Validation Rules:
1. `magic` MUST equal `kWSnapMagic` (`0x504E534F494E4957ULL`). Any other value triggers `SnapshotErrorCode::InvalidMagic`.
2. `format_version` MUST equal `1`. Unsupported versions trigger `SnapshotErrorCode::UnsupportedVersion`.
3. `header_crc32c` MUST match the computed CRC32C of bytes `0x00`..`0x3B`. Mismatch triggers `SnapshotErrorCode::HeaderCrcMismatch`.
4. `section_count` MUST be $\ge 1$ and $\le 1024$. Mismatch triggers `SnapshotErrorCode::MalformedSectionDirectory`.

---

## 4. Section Descriptor Schema (Fixed 40 Bytes)

The section directory immediately follows the 64-byte file header at offset `0x00000040`. It consists of `section_count` sequential 40-byte records:

| Byte Offset | Field Name | Type | Size | Description |
|---|---|---|---|---|
| `+0x00` | `section_type` | `uint32_t` | 4 bytes | Enum identifier (`SnapshotSectionType`). |
| `+0x04` | `core_id` | `uint32_t` | 4 bytes | Target core index (0 for global/system-wide sections). |
| `+0x08` | `data_offset` | `uint64_t` | 8 bytes | Absolute byte offset of payload within the `.wsnap` file. |
| `+0x10` | `data_size` | `uint64_t` | 8 bytes | Stored payload size in bytes. |
| `+0x18` | `uncompressed_size` | `uint64_t` | 8 bytes | Uncompressed payload size in bytes (equals `data_size` if uncompressed). |
| `+0x20` | `compression_type` | `uint32_t` | 4 bytes | Compression algorithm (`0 = None`, `1 = SparseChunks`). |
| `+0x24` | `payload_crc32c` | `uint32_t` | 4 bytes | CRC32C of the raw stored payload data. |

### Section Types:

```cpp
enum class SnapshotSectionType : uint32_t {
    Unknown       = 0,
    CpuState      = 1,   // Core GPRs, SPs, PC, PSTATE, SIMD, Sysregs, MMU
    MemoryState   = 2,   // Guest physical memory sparse chunks
    TimerState    = 3,   // VirtualTime counter and timer comparators
    GicState      = 4,   // GICv3 Distributor and Redistributor state
    PmuState      = 5,   // Performance Monitoring Unit counters & config
    DebugState    = 6,   // Hardware breakpoints, watchpoints, MDSCR
    Metadata      = 7,   // Human-readable labels, git commit, description
};
```

---

## 5. Directory and Section Validation Rules

The `SnapshotValidation` subsystem enforces strict pre-flight geometric and structural constraints:

1. **File Minimum Size**: File size must be at least $64 + (N \times 40)$ bytes.
2. **Directory Bounds**: Every section's `data_offset` must be strictly $\ge 64 + (N \times 40)$ bytes (payloads cannot overlap the directory).
3. **Payload Bounds**: For every section, $data\_offset + data\_size \le file\_size$.
4. **No Section Overlap**: For any two distinct sections $A$ and $B$, their byte spans $[offset_A, offset_A + size_A)$ and $[offset_B, offset_B + size_B)$ must not overlap:
   $$\max(offset_A, offset_B) \ge \min(offset_A + size_A, offset_B + size_B)$$
5. **Payload CRC32C**: The computed CRC32C over the file slice $[offset, offset + size)$ must strictly equal `payload_crc32c`. Any discrepancy halts restoration immediately.

---

## 6. Endianness and Serialization Rules

1. Every multi-byte integer (`uint16_t`, `uint32_t`, `uint64_t`) is serialized and deserialized using explicit little-endian byte operations (`encode_le32`, `encode_le64`, `decode_le32`, `decode_le64`).
2. Vector / 128-bit SIMD registers are serialized as two 64-bit halves: `low` (bits [63:0]) followed by `high` (bits [127:64]).
3. Booleans are serialized as 8-bit unsigned integers (`0x00 = false`, `0x01 = true`).
4. Padding fields are explicitly zero-initialized to guarantee deterministic file hash output across successive runs on identical state.
