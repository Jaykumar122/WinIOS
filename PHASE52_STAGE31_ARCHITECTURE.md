# WinIOS — Phase 52, Stage 31 Architecture
## Deterministic Device Virtualization, Virtual DMA Architecture & Replay-Safe I/O Foundation

---

## 1. Architectural Principles

Virtual devices and virtual DMA in WinIOS adhere to strict determinism, containment, and replay safety rules:
1. **Single-Host-Thread Synchronous Execution:** Virtual devices and DMA engines execute strictly on the single host thread running the scheduler (`SmpScheduler`). No secondary background host threads, kernel worker pools, or OS asynchronous I/O primitives are ever used.
2. **Explicit Memory Bounds:** The virtual DMA engine (`VirtualDmaEngine`) operates only on guest physical memory (`SmpSharedMemory`). All descriptor addresses, source buffers, and destination buffers are boundary-checked against `memory_size` before any write occurs.
3. **Atomic Failure Semantics:** If any error is detected in a descriptor chain (misalignment, bounds overflow, cyclic link, or length violation), the transaction is immediately rejected and zero destination bytes are modified.
4. **Cache & Monitor Coherency Invalidation:** When virtual DMA writes to guest physical RAM, it explicitly invokes `SmpExclusiveMonitorManager::invalidate_reservations`, clearing exclusive load reservations held by virtual cores on the affected granules.
5. **Replay Safety Invariant:** Uncaptured host DMA is strictly illegal. The capability flag `allow_uncaptured_dma = false` is immutable. DMA replay remains disabled by default (`allow_dma_replay = false`).

---

## 2. Component Architecture

```
+-------------------------------------------------------------------------+
|                              SmpScheduler                               |
|                  (Single Host Thread Deterministic Loop)                |
+-------------------------------------------------------------------------+
         |                                                       |
         v                                                       v
+-----------------------+                             +--------------------+
|   VirtualDeviceBus    |                             |  SmpSharedMemory   |
|  - MMIO Read/Write    |<------- MMIO Access --------|  - Guest RAM       |
|  - Overlap Detection  |                             |  - Bounds Checking |
|  - IRQ Dispatching    |                             +--------------------+
+-----------------------+                                        ^
         |                                                       |
         v                                                       |
+-----------------------+     Descriptor Fetch & Data Transfer   |
|   VirtualDmaDevice    |----------------------------------------+
|  - Regs 0x40000000    |
|  - Status / Control   |
|  - Telemetry Counters |
+-----------------------+
         |
         +--------------------> SmpInterruptRouter (SPI Delivery)
         |
         v
+-----------------------+
|   VirtualDmaEngine    |
|  - Chain Validation   |
|  - Cycle Detection    |
|  - Monitor Invalidate |-----> SmpExclusiveMonitorManager
+-----------------------+
```

### 2.1 Virtual Device Bus (`VirtualDeviceBus`)
- Maps non-overlapping MMIO ranges.
- Rejects misaligned read/write operations with `VirtualDeviceAccessResult::MisalignedAccess`.
- Provides registration and unregistration with overlap prevention (`std::max(start1, start2) < std::min(end1, end2)`).
- Automatically routes device interrupt requests to the `SmpInterruptRouter`.

### 2.2 Virtual DMA Engine (`VirtualDmaEngine`)
- **Descriptor Layout:** 64-byte descriptor (`alignas(8)`), containing `descriptor_id`, `src_paddr`, `dst_paddr`, `length_bytes`, `operation_type`, `flags`, `next_descriptor_paddr`, and `status`.
- **Validation:** Enforces 64-byte descriptor alignment, non-zero transfer lengths, maximum burst length of 16 MiB, arithmetic overflow prevention (`src + length`, `dst + length`), memory containment within guest physical RAM, and chain depth bounds (up to 256 links) with cycle detection via visited address tracking.
- **Operations:**
  - `Copy`: Memory-to-memory block transfer.
  - `Fill`: 32-bit pattern fill replicated across the destination buffer.
- **Atomic Rollback:** Validation is performed on source and destination addresses before any memory mutation. If validation fails, status is updated and no RAM bytes are altered.

### 2.3 Synthetic DMA Device (`VirtualDmaDevice`)
- Base address: `0x40000000` (Size: `0x1000`).
- Registers:
  - `0x00`: `REG_CONTROL` (Bit 0: Enable, Bit 1: Start, Bit 2: Reset, Bit 3: IRQ Enable)
  - `0x04`: `REG_STATUS` (Bit 0: Busy, Bit 1: Done, Bit 2: Error, Bit 3: IRQ Pending)
  - `0x08`: `REG_DESCRIPTOR_ADDR` (64-bit physical address of head descriptor)
  - `0x10`: `REG_TRANSFER_COUNT` (32-bit count of completed transfers)
  - `0x18`: `REG_BYTES_TRANSFERRED` (64-bit accumulator)
  - `0x20`: `REG_ERROR_CODE` (32-bit error code)
  - `0x24`: `REG_INTERRUPT_ID` (32-bit INTID, default 48)
  - `0xFC`: `REG_DEVICE_ID` (Read-only `0x56444D41` - ASCII 'VDMA')
- Execution is triggered synchronously by setting `kControlStart` in `REG_CONTROL`. Upon completion or failure, status registers are updated and an interrupt is optionally signaled.

---

## 3. SNDL v2 & WSnap Integration

### 3.1 SNDL v2 Recording Events
- Code 70: `DeviceRegisterRead`
- Code 71: `DeviceRegisterWrite`
- Code 72: `DmaSubmissionAccepted`
- Code 73: `DmaSubmissionRejected`
- Code 74: `DmaTransferCompleted`
- Code 75: `DmaTransferFailed`
- Code 76: `DeviceInterruptAsserted`
- Code 77: `DeviceStateTransition`

### 3.2 WSnap v2 Snapshot Format
- Section `0x18u` (`DeviceState`):
  - `device_id` (4 bytes, little-endian)
  - `payload_size` (4 bytes, little-endian)
  - `payload` (variable byte array)
  - `section_crc32c` (4 bytes)
- Device state payload includes 64 bytes covering control, status, descriptor pointer, byte counters, error codes, and interrupt lines.
