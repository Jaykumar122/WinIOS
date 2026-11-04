# SNDL v2 Binary Format Specification

## 1. File Layout
An SNDL v2 binary stream (`.sndl`) consists of:
1. Fixed 64-byte File Header (`WSndlV2Header`).
2. A sequence of Variable/Fixed Blocks, each comprising a 48-byte Block Header (`WSndlV2BlockHeader`) followed by an array of 80-byte Event Records (`SndlV2Record`).
3. Fixed 64-byte Integrity Footer (`WSndlV2Footer`).

```
+-----------------------------------------------------------------------+
| File Header: WSndlV2Header (64 bytes)                                 |
+-----------------------------------------------------------------------+
| Block 0 Header: WSndlV2BlockHeader (48 bytes)                         |
|   Record 0: SndlV2Record (80 bytes)                                   |
|   Record 1: SndlV2Record (80 bytes)                                   |
|   ...                                                                 |
|   Record N-1: SndlV2Record (80 bytes)                                 |
+-----------------------------------------------------------------------+
| Block 1 Header: WSndlV2BlockHeader (48 bytes)                         |
|   ...                                                                 |
+-----------------------------------------------------------------------+
| File Footer: WSndlV2Footer (64 bytes)                                 |
+-----------------------------------------------------------------------+
```

---

## 2. Magic Identifiers & Constants
- File Magic: `{'W', 'S', 'N', 'D', '2', '\0', '\1', '\0'}` (8 bytes)
- Block Magic: `{'S', '2', 'B', 'K'}` (4 bytes)
- Footer Magic: `{'W', 'S', 'N', 'D', '2', 'F', 'T', 'R'}` (8 bytes)
- Format Version: `kWSndlVersion2 = 2`

---

## 3. WSndlV2Header (Fixed 64 Bytes)
| Offset | Field | Type | Description |
|---|---|---|---|
| 0..7 | magic | u8[8] | Fixed magic `WSND2\0\1\0` |
| 8..9 | version | u16 | Format version (must be 2) |
| 10..11 | core_count | u16 | Virtual cores in cluster ($1 \dots 256$) |
| 12..13 | flags | u16 | Format flags |
| 14..15 | compression_type | u16 | Compression algorithm (0 = None) |
| 16..23 | base_snapshot_hash | u64 | Canonical hash of baseline `.wsnap v2` |
| 24..31 | base_snapshot_file_size | u64 | Size of baseline `.wsnap v2` in bytes |
| 32..39 | memory_capacity | u64 | Guest physical memory size in bytes |
| 40..47 | initial_global_sequence | u64 | Initial global event sequence counter |
| 48..51 | total_events | u32 | Total event records (updated on finalization) |
| 52..55 | total_blocks | u32 | Total blocks written (updated on finalization) |
| 56..59 | reserved | u32 | Zero padding |
| 60..63 | header_crc32c | u32 | CRC32C checksum over bytes [0..59] |

---

## 4. WSndlV2BlockHeader (Fixed 48 Bytes)
| Offset | Field | Type | Description |
|---|---|---|---|
| 0..3 | magic | u8[4] | Block magic `S2BK` |
| 4..5 | version | u16 | Block format version (2) |
| 6..7 | flags | u16 | Block flags |
| 8..15 | first_global_sequence | u64 | First event sequence in block |
| 16..23 | last_global_sequence | u64 | Last event sequence in block |
| 24..27 | block_index | u32 | 0-based sequential block index |
| 28..31 | record_count | u32 | Count of 80-byte records in block |
| 32..35 | payload_bytes | u32 | `record_count * 80` |
| 36..39 | payload_crc32c | u32 | CRC32C checksum over payload bytes |
| 40..43 | reserved | u32 | Alignment padding |
| 44..47 | header_crc32c | u32 | CRC32C checksum over bytes [0..43] |

---

## 5. SndlV2Record (Uniform 80 Bytes)
| Offset | Field | Type | Description |
|---|---|---|---|
| 0..7 | global_event_sequence | u64 | Strictly monotonic global event index ($1, 2, 3\dots$) |
| 8..15 | global_retirement_sequence | u64 | Cluster-wide instruction retirement count |
| 16..23 | core_retirement_count | u64 | Per-core instruction retirement count |
| 24..31 | pc | u64 | Architectural program counter at event boundary |
| 32..39 | payload1 | u64 | Primary payload (address, intid, quantum budget) |
| 40..47 | payload2 | u64 | Secondary payload (value, priority, reason code) |
| 48..55 | payload3 | u64 | Tertiary payload (access width, flags, status) |
| 56..59 | core_id | u32 | Originating virtual core index |
| 60..63 | source_id | u32 | Hardware device or source channel identifier |
| 64..67 | flags | u32 | Event-specific flags |
| 68..69 | sub_instruction_ordinal | u16 | Sub-instruction order index (0 = primary) |
| 70..71 | event_type | u16 | Type enum (`SndlV2EventType`) |
| 72 | boundary_type | u8 | Boundary enum (`SndlV2BoundaryType`) |
| 73 | reserved | u8 | Zero padding |
| 74..75 | payload_size | u16 | Byte count of represented payload |
| 76..79 | record_crc32c | u32 | CRC32C checksum over bytes [0..75] |

---

## 6. WSndlV2Footer (Fixed 64 Bytes)
| Offset | Field | Type | Description |
|---|---|---|---|
| 0..7 | magic | u8[8] | Fixed magic `WSND2FTR` |
| 8..15 | last_global_sequence | u64 | Highest sequence number recorded |
| 16..23 | last_global_retirement_sequence | u64 | Highest global retirement sequence reached |
| 24..31 | total_events | u64 | Cumulative event count in file |
| 32..39 | dropped_events | u64 | Dropped events if buffer overflowed |
| 40..43 | total_blocks | u32 | Total blocks written |
| 44..45 | version | u16 | Footer format version (2) |
| 46..47 | completion_status | u16 | `1 = Completed`, `2 = Aborted`, `3 = Overflow` |
| 48..59 | reserved | u8[12] | Zero padding |
| 60..63 | footer_crc32c | u32 | CRC32C checksum over bytes [0..59] |
