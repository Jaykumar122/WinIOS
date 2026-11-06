# WinIOS — WSnap v2 Device State Snapshot Specification

## 1. Overview
Virtual device state persistence is integrated into the `.wsnap v2` cluster snapshot format using Section ID `0x18u` (`DeviceState`).

## 2. Section Header & Framing
```
+------------------------------------+
| Section Type: 0x0018 (uint16_t)    |
| Reserved:     0x0000 (uint16_t)    |
| Section Size: (uint32_t)           |
+------------------------------------+
| Device ID:    (uint32_t)           |
| Payload Size: (uint32_t)           |
| Payload Data: (N bytes)            |
| Section CRC:  (uint32_t, CRC32C)   |
+------------------------------------+
```

## 3. DMA Device Payload Format (64 Bytes)
| Offset | Field | Type | Description |
|---|---|---|---|
| `0x00` | `magic` | `uint32_t` | `0x56444D41` ('VDMA') |
| `0x04` | `status_flags` | `uint32_t` | Control & status bitfield |
| `0x08` | `head_descriptor_addr` | `uint64_t` | Configured descriptor physical address |
| `0x10` | `transfer_count` | `uint32_t` | Completed chain counter |
| `0x14` | `error_code` | `uint32_t` | Last encountered error code |
| `0x18` | `bytes_transferred` | `uint64_t` | Total bytes moved |
| `0x20` | `irq_line` | `uint32_t` | Configured interrupt line |
| `0x24` | `irq_asserted` | `uint8_t` | Interrupt line state (0 or 1) |
| `0x25` | `reserved` | `uint8_t[39]` | Pad to 64 bytes |

## 4. Integrity Verification
Deserialization computes a CRC32C checksum over the Section ID, Device ID, Payload Size, and Payload Data. Any mismatch results in immediate restoration rejection (`SnapshotErrorCode::SectionCrcMismatch`).
