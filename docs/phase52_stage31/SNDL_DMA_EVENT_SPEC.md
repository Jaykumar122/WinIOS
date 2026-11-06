# WinIOS — SNDL v2 Virtual DMA & Device Event Specification

## 1. Event Codes

SNDL v2 assigns event IDs in the range 70–77 for virtual devices and virtual DMA:

| Event ID | Event Identifier | Description | Payload Structure |
|---|---|---|---|
| 70 | `DeviceRegisterRead` | Read from virtual device MMIO register | `device_id (u32), offset (u32), size (u8), value (u64)` |
| 71 | `DeviceRegisterWrite` | Write to virtual device MMIO register | `device_id (u32), offset (u32), size (u8), value (u64)` |
| 72 | `DmaSubmissionAccepted`| DMA descriptor chain accepted for execution | `device_id (u32), head_desc_addr (u64), core_id (u32)` |
| 73 | `DmaSubmissionRejected`| DMA submission rejected during validation | `device_id (u32), head_desc_addr (u64), error_code (u32)` |
| 74 | `DmaTransferCompleted` | DMA descriptor chain completed successfully | `device_id (u32), descriptors_processed (u32), bytes_transferred (u64)` |
| 75 | `DmaTransferFailed` | DMA descriptor execution failed mid-chain | `device_id (u32), failed_desc_addr (u64), error_code (u32)` |
| 76 | `DeviceInterruptAsserted` | Device asserted an interrupt line | `device_id (u32), irq_line (u32)` |
| 77 | `DeviceStateTransition` | Device status/mode transition | `device_id (u32), prev_status (u32), new_status (u32)` |

## 2. Global Ordering & Sequencing
Every device and DMA event carries the global retirement sequence counter, guaranteeing exact interleaving with instruction execution and multicore scheduler quanta.
