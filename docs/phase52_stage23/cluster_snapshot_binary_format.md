# Cluster Snapshot Binary Format Specification (.wsnap v2)

## 1. Specification Overview

The `.wsnap` version 2 binary container encapsulates multicore cluster execution state. All multi-byte integers are serialized in Little-Endian format. Integrity is protected via Castagnoli CRC32C polynomial (`0x1EDC6F41`).

---

## 2. File Layout

```
+-------------------------------------------------------------------+
| WSnapHeader (64 bytes)                                            |
| Magic: 'WSNAP\0\1\0' | Version: 2 | Core Count: N | Mem Cap       |
+-------------------------------------------------------------------+
| Section 0 Payload (METADATA - 0x07)                               |
+-------------------------------------------------------------------+
| Section 1 Payload (CORE_DIRECTORY - 0x11)                         |
+-------------------------------------------------------------------+
| Section 2..N+1 Payloads (CORE_ARCHITECTURAL_STATE - 0x12)         |
+-------------------------------------------------------------------+
| Section N+2 Payload (SHARED_MEMORY - 0x13)                        |
+-------------------------------------------------------------------+
| Section N+3 Payload (SCHEDULER - 0x14)                            |
+-------------------------------------------------------------------+
| Section N+4 Payload (EXCLUSIVE_MONITOR - 0x15)                    |
+-------------------------------------------------------------------+
| Section N+5 Payload (INTERRUPT_ROUTER - 0x16)                     |
+-------------------------------------------------------------------+
| Section N+6 Payload (INTEGRITY - 0x17)                            |
+-------------------------------------------------------------------+
| Section Directory Table (SectionCount * 40 bytes)                 |
+-------------------------------------------------------------------+
```

---

## 3. Header Schema (64 Bytes)

| Offset | Field | Type | Description |
|---|---|---|---|
| 0x00 | `magic` | `uint8[8]` | Fixed ASCII `'W','S','N','A','P','\0','\1','\0'` |
| 0x08 | `version` | `uint16` | Format version (`2` for cluster snapshots) |
| 0x0A | `core_count` | `uint16` | Number of virtual cores in cluster ($N \ge 1$) |
| 0x0C | `compression_type`| `uint16` | Compression (`0` = None) |
| 0x0E | `flags` | `uint16` | Bitmask flags |
| 0x10 | `timestamp_ns` | `uint64` | Diagnostic capture timestamp (UNIX epoch ns) |
| 0x18 | `memory_capacity` | `uint64` | Shared physical memory capacity in bytes |
| 0x20 | `section_dir_offset` | `uint64`| File byte offset to Section Directory Table |
| 0x28 | `section_count` | `uint32` | Number of section descriptors |
| 0x2C | `total_file_size` | `uint32` | Exact file size in bytes |
| 0x30 | `reserved0..2` | `uint32[3]` | Reserved fields (zero initialized) |
| 0x3C | `header_crc32c` | `uint32` | CRC32C over bytes 0x00..0x3B (60 bytes) |

---

## 4. Section Descriptor Schema (40 Bytes)

| Offset | Field | Type | Description |
|---|---|---|---|
| 0x00 | `section_type` | `uint32` | Section type identifier |
| 0x04 | `section_id` | `uint32` | Sub-identifier (e.g. core ID for per-core state) |
| 0x08 | `file_offset` | `uint64` | File offset of payload |
| 0x10 | `compressed_size` | `uint64` | Payload size in file |
| 0x18 | `uncompressed_size`| `uint64` | Uncompressed payload size |
| 0x20 | `section_crc32c` | `uint32` | CRC32C over payload bytes |
| 0x24 | `flags` | `uint16` | Section-specific flags |
| 0x26 | `reserved` | `uint16` | Alignment padding |

---

## 5. Section Type Identifiers

| Identifier | Value | Section Name | Description |
|---|---|---|---|
| `Metadata` | `0x07` | METADATA | Description, creator, cluster topology strings |
| `CoreDirectory` | `0x11` | CORE_DIRECTORY | Core count, IDs, lifecycle and active status |
| `CoreArchitecturalState` | `0x12` | CORE_ARCHITECTURAL_STATE | 1056-byte complete architectural register state per core |
| `SharedMemory` | `0x13` | SHARED_MEMORY | Sparse 4KB chunk physical memory representation |
| `Scheduler` | `0x14` | SCHEDULER | Quantum $Q$, global retirement sequence, slot metrics |
| `ExclusiveMonitor` | `0x15` | EXCLUSIVE_MONITOR | Cluster reservation granules, exact addresses, valid flags |
| `InterruptRouter` | `0x16` | INTERRUPT_ROUTER | Per-core FIFO pending interrupt queues |
| `Integrity` | `0x17` | INTEGRITY | Canonical cluster state hash and section CRC table |
