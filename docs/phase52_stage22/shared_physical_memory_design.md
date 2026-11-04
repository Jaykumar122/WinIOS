# Shared Physical Memory Design & Architecture

## 1. Architectural Scope

In a symmetric multiprocessing (SMP) system, all virtual cores share a single, unified physical address space. A memory store performed by Core $A$ must be immediately visible to Core $B$ upon memory access, subject to architectural memory ordering and exclusive monitor invalidation rules.

The `SmpSharedMemory` subsystem (`src/cpu/smp_shared_memory.h`, `src/cpu/smp_shared_memory.cpp`) provides centralized physical memory management, core-tagged access operations, diagnostic telemetry, and automatic coordination with the system exclusive monitor.

---

## 2. Memory Architecture & Address Identity

```
+-------------------------------------------------------------+
|                      SmpSharedMemory                         |
|                                                             |
|  +-----------------+  +-----------------+  +-------------+  |
|  |  Virtual Core 0 |  |  Virtual Core 1 |  | Core N...   |  |
|  +--------+--------+  +--------+--------+  +------+------+  |
|           |                    |                  |         |
|           +--------------------+------------------+         |
|                                |                            |
|             Core-Tagged Accesses (read_uX / write_uX)       |
|                                |                            |
|                                v                            |
|           +-------------------------------------+           |
|           |  Underlying Unified Physical RAM    |           |
|           |  (Memory: Flat Little-Endian Store) |           |
|           +------------------+------------------+           |
|                              |                              |
|                    On Stores | Invalidate                   |
|                              v                              |
|           +-------------------------------------+           |
|           |    SmpExclusiveMonitorManager       |           |
|           |    (64-Byte Cacheline Granules)     |           |
|           +-------------------------------------+           |
+-------------------------------------------------------------+
```

### Key Architectural Properties:
1. **Address Space Identity**: All virtual cores map to identical physical byte addresses in the underlying RAM buffer. Accessing physical address $A$ from Core 0 addresses the exact same byte storage as accessing physical address $A$ from Core 1.
2. **Sequential Consistency in Single Host Thread Mode**: Because all virtual cores are interleaved deterministically on a single host thread, stores take effect sequentially in global retirement order without inter-thread memory barriers or store-buffer buffering.
3. **Multi-Width Access Primitives**:
   - `read_u8`, `write_u8` (1 byte)
   - `read_u16`, `write_u16` (2 bytes, little-endian)
   - `read_u32`, `write_u32` (4 bytes, little-endian)
   - `read_u64`, `write_u64` (8 bytes, little-endian)
   - `read_bytes`, `write_bytes` (Arbitrary length byte blocks)
4. **Out-of-Bounds Enforcement**: Accesses extending beyond `size()` throw `std::out_of_range`.

---

## 3. Exclusive Monitor Notification Hook

Whenever any virtual core commits a store (`write_u8/16/32/64` or `write_bytes`):
1. The store is written directly to the underlying physical memory buffer.
2. If an `SmpExclusiveMonitorManager` is attached, the memory subsystem calls:
   ```cpp
   exclusive_monitor_->invalidate_reservations(writer_core_id, address, size);
   ```
3. Any active exclusive reservation held by any other virtual core that overlaps the 64-byte cacheline granule `[address & ~0x3F, (address & ~0x3F) + 64)` is invalidated atomically with the store.

---

## 4. Per-Core Diagnostic Telemetry

To facilitate debugging, performance profiling, and memory bandwidth characterization, `SmpSharedMemory` maintains independent non-architectural access counters for each virtual core:
- `reads`: Total read operations performed by this core.
- `writes`: Total write operations performed by this core.
- `bytes_read`: Total bytes transferred from physical memory to this core.
- `bytes_written`: Total bytes transferred from this core to physical memory.

**Non-Architectural Property**: Telemetry counters exist purely for diagnostics and reporting. They are not hashed into canonical state hashes and do not alter guest memory state or execution behavior.
