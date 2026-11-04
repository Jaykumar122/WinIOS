# DMA Capture Architecture & BLK-SMP-03 Analysis

## 1. Subsystem Audit: DMA Capabilities in WinIOS

In accordance with Stage 27 instructions, an audit of the WinIOS codebase was performed to determine the existence of DMA-capable devices, asynchronous host device threads, or background memory injection.

### Audit Findings
1. **Zero DMA Peripherals**: The current WinIOS codebase contains no DMA-capable virtual controllers (e.g. no virtio-net, virtio-blk, or simulated NVMe/PCIe bus mastering).
2. **Synchronous Programmed I/O Only**: All peripheral communication is implemented as synchronous MMIO read/write operations handled within the instruction stepping loop.
3. **No Background Worker Threads**: Emulation runs strictly on a single host thread under `SmpScheduler`.

---

## 2. Proposed DMA Event Architecture (For Future Hardware Stages)

When DMA controllers are introduced in future stages, deterministic DMA capture must adhere to the following architecture:

### 2.1 Proposed SNDL v3 / Companion Data Event Format
```cpp
struct DmaTransferRecord {
    uint64_t global_event_sequence;
    uint32_t device_id;
    uint32_t transfer_id;
    uint64_t guest_physical_address;
    uint64_t transfer_length;
    uint32_t payload_crc32c;
    uint64_t completion_retirement_sequence;
};
```

### 2.2 Alignment to Quantum Boundaries
To preserve reproducible memory interleaving:
- DMA transfers must not occur asynchronously during a CPU quantum slice.
- DMA transfers must be committed at scheduler quantum boundaries ($Q_{end}$).
- A memory barrier sync record (`MemoryBarrierSync`) must be emitted following buffer injection.
