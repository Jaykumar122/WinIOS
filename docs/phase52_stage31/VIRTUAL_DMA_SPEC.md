# WinIOS — Virtual DMA Specification

## 1. Overview
The Virtual DMA subsystem provides high-performance, strictly bounded, deterministic memory transfer operations between regions in guest physical memory.

## 2. Descriptor Specification
Descriptors are 64 bytes in length and must be aligned to 64 bytes (`alignas(8)`).

| Field | Type | Offset | Description |
|---|---|---|---|
| `descriptor_id` | `uint32_t` | `0x00` | Unique transfer descriptor identifier |
| `operation_type` | `uint32_t` | `0x04` | `0 = Copy`, `1 = Fill` |
| `flags` | `uint32_t` | `0x08` | `0x1 = InterruptOnComplete`, `0x2 = Chain` |
| `status` | `uint32_t` | `0x0C` | Output status (0=Pending, 1=Success, 2=Error) |
| `src_paddr` | `uint64_t` | `0x10` | Source physical address in guest RAM |
| `dst_paddr` | `uint64_t` | `0x18` | Destination physical address in guest RAM |
| `length_bytes` | `uint64_t` | `0x20` | Transfer size in bytes |
| `fill_pattern` | `uint32_t` | `0x28` | Pattern replicated across destination if Fill op |
| `error_code` | `uint32_t` | `0x2C` | Output error code if transfer fails |
| `next_descriptor_paddr` | `uint64_t` | `0x30` | Address of next descriptor if Chained |
| `reserved` | `uint8_t[16]` | `0x38` | Reserved (must be zero) |

## 3. Limits & Bounds Checking
- Minimum length: 1 byte
- Maximum length: 16 MiB (`VirtualDmaLimits::kMaxTransferLength = 16 * 1024 * 1024`)
- Maximum chain depth: 256 descriptors (`VirtualDmaLimits::kMaxChainDepth = 256`)
- Cyclic chain detection: Monitored via visited descriptor set; cycles immediately trigger error code `CyclicDescriptorChain`.
- Memory bounds: Source and destination ranges must satisfy:
  `paddr + length <= memory_size` without arithmetic wrap-around.

## 4. Execution Semantics
- Executed synchronously on the scheduler thread.
- Atomic commit: validation failures leave the destination buffer completely unaltered.
- Exclusive monitor invalidation: Stores to guest RAM invalidate exclusive reservations on overlapping 64-byte granules across all SMP cores.
