# WinIOS Phase 52 Stage 18: SNDL Binary Format Specification

## 1. Specification Overview

The Sparse Non-Deterministic Log format (`.sndl`) is a cacheline-aligned, block-oriented binary stream format engineered specifically for zero-overhead recording and high-throughput validation of nondeterministic execution events.

### 1.1 Structural Summary
- **Magic**: `WSNDL\0\1\0` (ASCII + null-terminators).
- **Format Version**: `1` (`kWSndlCurrentVersion`).
- **File Extension**: `.sndl`.
- **Endianness**: Explicit Little-Endian for all multi-byte fields.
- **Header**: Fixed 64 bytes, validated with CRC32C (Castagnoli polynomial `0x1EDC6F41`).
- **Block Organization**: Records are batched into blocks (default 1024 records per block).
- **Block Header**: Fixed 48 bytes (`SBLK` magic), validated with header CRC32C and payload CRC32C.
- **Record Size**: Exactly 64 bytes per record (`kWSndlRecordDiskSize`), cacheline aligned.
- **Footer**: Fixed 64 bytes (`WSNDLFTR`), recording finalization status, total record counts, last sequence, and total retired instructions.

---

## 2. File Organization Diagram

```
+---------------------------------------------------------------+
| WSndlHeader (64 bytes)                                        |
| Magic: "WSNDL\0\1\0" | Version: 1 | Cores | Hash | CRC32C     |
+---------------------------------------------------------------+
| Block 0: WSndlBlockHeader (48 bytes, "SBLK")                  |
| Record Count: N | Payload Bytes: N*64 | Payload CRC32C        |
+---------------------------------------------------------------+
|   Record 0 (64 bytes, SndlRecord)                             |
|   Record 1 (64 bytes, SndlRecord)                             |
|   ...                                                         |
|   Record N-1 (64 bytes, SndlRecord)                           |
+---------------------------------------------------------------+
| Block 1 ...                                                   |
+---------------------------------------------------------------+
| WSndlFooter (64 bytes, "WSNDLFTR")                            |
| Status: Completed | Block Count | Total Records | CRC32C      |
+---------------------------------------------------------------+
```

---

## 3. Binary Structures

### 3.1 WSndlHeader (Fixed 64 Bytes)

| Offset (Bytes) | Field Name | Type | Description |
|---|---|---|---|
| `0x00 - 0x07` | `magic` | `char[8]` | `"WSNDL\0\1\0"` |
| `0x08 - 0x09` | `version` | `uint16_t` | Format version (must be `1`) |
| `0x0A - 0x0B` | `core_count` | `uint16_t` | Number of SMP cores in guest instance |
| `0x0C - 0x0F` | `flags` | `uint32_t` | Architecture and recording flags |
| `0x10 - 0x17` | `base_snapshot_hash` | `uint64_t` | CRC32C / hash of baseline `.wsnap` file |
| `0x18 - 0x1F` | `creation_timestamp_ns`| `uint64_t` | System clock timestamp in nanoseconds |
| `0x20 - 0x27` | `total_record_count` | `uint64_t` | Total records in file (updated on finalize) |
| `0x28 - 0x2F` | `total_instructions` | `uint64_t` | Total retired instructions (updated on finalize)|
| `0x30 - 0x3B` | `snapshot_id` | `uint8_t[12]` | 12-byte unique baseline snapshot identifier |
| `0x3C - 0x3F` | `header_crc32c` | `uint32_t` | CRC32C computed over first 60 bytes |

### 3.2 WSndlBlockHeader (Fixed 48 Bytes)

| Offset (Bytes) | Field Name | Type | Description |
|---|---|---|---|
| `0x00 - 0x03` | `magic` | `char[4]` | `"SBLK"` |
| `0x04 - 0x05` | `version` | `uint16_t` | Block version (1) |
| `0x06 - 0x07` | `flags` | `uint16_t` | Block compression / encryption flags (0 = uncompressed)|
| `0x08 - 0x0B` | `record_count` | `uint32_t` | Number of 64-byte records in this block |
| `0x0C - 0x0F` | `payload_bytes` | `uint32_t` | Total bytes of records in block (`record_count * 64`) |
| `0x10 - 0x17` | `first_sequence` | `uint64_t` | Global sequence number of first record in block |
| `0x18 - 0x1F` | `last_sequence` | `uint64_t` | Global sequence number of last record in block |
| `0x20 - 0x23` | `payload_crc32c` | `uint32_t` | CRC32C computed across all record payload bytes |
| `0x24 - 0x2B` | `reserved` | `uint8_t[8]` | Reserved, zero-padded |
| `0x2C - 0x2F` | `header_crc32c` | `uint32_t` | CRC32C computed over first 44 bytes |

### 3.3 SndlRecord (Fixed 64 Bytes)

Every nondeterministic event is serialized into a uniform, fixed 64-byte schema:

| Offset (Bytes) | Field Name | Type | Description |
|---|---|---|---|
| `0x00 - 0x07` | `sequence_num` | `uint64_t` | Globally monotonic sequence number across all cores |
| `0x08 - 0x0F` | `instruction_ret_count` | `uint64_t` | Instruction retirement count on core when event occurred |
| `0x10 - 0x17` | `pc` | `uint64_t` | Architectural program counter |
| `0x18 - 0x1F` | `payload1` | `uint64_t` | Primary parameter (paddr, intid, reg_key, etc.) |
| `0x20 - 0x27` | `payload2` | `uint64_t` | Secondary parameter (read value, priority, counter) |
| `0x28 - 0x2F` | `payload3` | `uint64_t` | Tertiary parameter (width, flags, frequency) |
| `0x30 - 0x33` | `source_id` | `uint32_t` | Injecting device, source subsystem, or channel ID |
| `0x34 - 0x37` | `flags` | `uint32_t` | Event-specific flags |
| `0x38 - 0x39` | `core_id` | `uint16_t` | CPU core identifier (0..N-1) |
| `0x3A - 0x3B` | `sub_instruction_ordinal`| `uint16_t` | Causal tie-breaker if multiple events at same retirement |
| `0x3C` | `event_type` | `uint8_t` | Event type (`SndlEventType`, 1..9) |
| `0x3D` | `boundary_type` | `uint8_t` | Execution boundary (`SndlBoundaryType`, 0..5) |
| `0x3E - 0x3F` | `payload_size` | `uint16_t` | Valid bytes in payload (1, 2, 4, 8) |

### 3.4 WSndlFooter (Fixed 64 Bytes)

| Offset (Bytes) | Field Name | Type | Description |
|---|---|---|---|
| `0x00 - 0x07` | `magic` | `char[8]` | `"WSNDLFTR"` |
| `0x08 - 0x09` | `version` | `uint16_t` | Footer version (1) |
| `0x0A - 0x0B` | `finalization_status` | `uint16_t` | Status (`Completed` = 1, `Overflow` = 2, `Aborted` = 3) |
| `0x0C - 0x0F` | `block_count` | `uint32_t` | Total number of blocks written to file |
| `0x10 - 0x17` | `total_records` | `uint64_t` | Total records across all blocks |
| `0x18 - 0x1F` | `last_sequence` | `uint64_t` | Final global sequence number |
| `0x20 - 0x27` | `total_instructions` | `uint64_t` | Total retired instructions across cores |
| `0x28 - 0x3B` | `reserved` | `uint8_t[20]` | Reserved, zero-padded |
| `0x3C - 0x3F` | `footer_crc32c` | `uint32_t` | CRC32C computed over first 60 bytes |

---

## 4. Checksum Algorithm: CRC32C (Castagnoli)

All headers, blocks, and footers are protected by 32-bit Castagnoli CRC (`0x1EDC6F41`):
- Polynomial: `x^32 + x^28 + x^27 + x^26 + x^25 + x^23 + x^22 + x^20 + x^19 + x^18 + x^14 + x^13 + x^11 + x^10 + x^9 + x^8 + x^6 + 1`
- Reflected polynomial: `0x82F63B78`
- Initial remainder: `0xFFFFFFFF`
- Final XOR: `0xFFFFFFFF`
- Hardware Acceleration: Uses SSE4.2 / ARMv8 CRC instructions when available; falls back to verified unrolled table algorithm.
