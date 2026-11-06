# WinIOS — WSnap v2 DeviceState Format Specification
## Snapshot Section 0x18 Binary Structure

---

## 1. Overview

Section `0x18` (`DeviceState`) in `.wsnap v2` snapshot archives serializes the state of all devices attached to the `VirtualDeviceBus`. This enables full cluster state restoration prior to replaying recorded SNDL v2 logs.

---

## 2. Binary Layout

### 2.1 Section Container Framing
```
+-----------------------------------+-----------------------------------+
| Section Type: 0x0018 (uint32_t)   | Section Flags: 0x0000 (uint32_t)  |
+-----------------------------------+-----------------------------------+
| Payload Length (uint64_t)         | Payload CRC32C (uint32_t)         |
+-----------------------------------+-----------------------------------+
| Payload Data: Device State Array                                      |
+-----------------------------------------------------------------------+
```

### 2.2 Device State Record Format
For each device on the bus:
- `device_id` (uint32_t): Unique device identifier.
- `device_type` (uint32_t): `1` = Virtual DMA Device.
- `state_size` (uint32_t): Size of device-specific state payload (bytes).
- `enabled` (uint8_t): Device enable state.
- `reserved` (uint8_t[3]): Alignment padding.
- `state_payload`: Device-specific serialized registers.

### 2.3 Virtual DMA Device State Payload (64 bytes)
```
Offset  Size  Field
0x00    8     reg_control (Control register)
0x08    8     reg_status (Status register)
0x10    8     reg_descriptor_addr (Current descriptor pointer)
0x18    8     reg_interrupt_id (Assigned IRQ line)
0x20    8     reg_transfer_count (Total descriptors completed)
0x28    8     reg_bytes_transferred (Total bytes committed)
0x30    8     reg_error_code (Last error code)
0x38    8     reserved (Zero padding)
```
CRC32C is computed over the serialized payload to ensure tamper-proof restoration.
