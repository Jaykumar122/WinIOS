# WinIOS Campaign 198: Architecture Decision Records (ADRs)

**Milestone:** Campaign 198 — ARM64 Memory-System Integration, Cache Coherency, DMA Consistency & SMP Virtual Platform Conformance  
**Target Research Architecture:** ARMv8.1-A / ARM64e (`iPhone15,4` / Apple A16 Bionic `T8120`)  
**Host Environment:** Windows 11 x64, Visual Studio 2026, MSVC C++20 (`/W4 /WX /permissive-`)  
**Classification:** `GUEST_MEMORY_MODEL` / `GUEST_CACHE` / `GUEST_DMA` / `SYNTHETIC_KERNEL`  

---

## ADR-198-01: Unified Guest Memory Access Layer (`UnifiedMemoryView`)

### Context
In previous campaigns, different CPU instructions, devices, and debug tools accessed physical RAM or translated virtual memory through disparate, uncoordinated access paths. Crucially, operations such as `DC ZVA` directly modified physical memory without performing MMU translation or permission verification, creating translation bypass defects and leaving reservation monitors un-snooped.

### Decision
Introduce `winios::memory::UnifiedMemoryView` in `src/memory/unified_memory.h` and `unified_memory.cpp` as the authoritative single entry point for virtual memory operations.
1. All guest virtual loads, stores, and block zeroing operations must route through `UnifiedMemoryView`.
2. Translates virtual addresses to physical addresses through the core's `MMU`, enforcing page permissions (`AP[2:1]`, `PXN`, `UXN`).
3. Enforces architectural alignment rules according to `SCTLR_EL1.A`: unaligned accesses on Normal memory are permitted when `SCTLR_EL1.A = 0`, but strictly fault with Alignment Fault (`ESR_EL1.EC = 0x21 / 0x25`) when `SCTLR_EL1.A = 1` or on any Device memory type.
4. Handles 16 KB page boundary crossings via byte-stitching across contiguous physical pages.
5. Invalidates conflicting exclusive reservations on the destination 64-byte reservation granule across remote cores.

### Consequences
- Resolves the translation bypass defect identified in Stage 198.0 audit.
- Centralizes alignment, watchpoint, memory attribute, and reservation invalidation checks.

---

## ADR-198-02: ARMv8 Memory Attribute Encoding & Device Memory Restrictions

### Context
ARMv8 defines distinct memory types through the Translation Table Descriptor `AttrIndx[2:0]` mapped via `MAIR_EL1` (Memory Attribute Indirection Register), and 2-bit shareability fields `SH[1:0]`. Device memory types (`Device_nGnRnE`, `Device_nGnRE`, `Device_nGRE`, `Device_GRE`) impose strict architectural constraints that differ from Normal memory.

### Decision
Implement `winios::memory::decode_memory_attributes` in `src/memory/memory_attributes.h` and `memory_attributes.cpp`.
1. Decodes MAIR attribute bytes into `MemoryType::Normal` vs `MemoryType::Device` with precise sub-classifications:
   - `Device_nGnRnE` (0x00): non-Gathering, non-Reordering, non-Early-Write-Acknowledgement.
   - `Device_nGnRE` (0x04): non-Gathering, non-Reordering, Early-Write-Acknowledgement.
   - `Device_nGRE` (0x08): non-Gathering, Reordering, Early-Write-Acknowledgement.
   - `Device_GRE` (0x0C): Gathering, Reordering, Early-Write-Acknowledgement.
2. Enforce strict restrictions on Device memory:
   - Unaligned accesses are unconditionally forbidden (`allow_unaligned = false`).
   - Atomic read-modify-write operations (`CAS`, `SWP`, `LDADD`, etc.) are forbidden (`allow_atomics = false`).
   - Block cache operations (`DC ZVA`) fault if issued to Device memory (`allow_dc_zva = false`).
3. For Normal memory, inner and outer cacheability (`NonCacheable`, `WriteThrough`, `WriteBack`) and shareability domains (`NonShareable`, `InnerShareable`, `OuterShareable`) are extracted and exposed for memory system simulation.

### Consequences
- Eliminates non-architectural unaligned or atomic operations on simulated MMIO devices.
- Provides faithful behavior matching Apple silicon Device mapping specifications.

---

## ADR-198-03: Cache Line & Reservation Granule Architecture

### Context
Apple A16 Bionic (`T8120`) features 64-byte L1 data and instruction cache lines, and a 64-byte exclusive reservation granule (`CTR_EL0.DminLine = 4`, `CTR_EL0.IminLine = 4`, `CTR_EL0.ERG = 4`). Previous code used inconsistent granule sizes (16B, 32B, 64B) in different parts of the emulator.

### Decision
Standardize all cache lines, zero-block operations, and exclusive reservation monitors to 64 bytes (`kCacheLineBytes = 64`):
1. `CTR_EL0`: `0x8444C004` (64-byte D-cache line, 64-byte I-cache line, 64-byte exclusive reservation granule).
2. `DCZID_EL0`: `0x00000004` (`DZP = 0`, `BS = 4` -> $2^4 \times 4 = 64$ bytes).
3. `DC CIVAC` and `DC IVAC` invalidate 64-byte aligned blocks; `DC CIVAC` requires Write permission, whereas `DC CVAC` requires Read permission.
4. `IC IVAU` invalidates the instruction cache line at PoU for 64 bytes.

### Consequences
- Architectural self-modifying code sequences (`STR` -> `DC CVAU` -> `DSB ISH` -> `IC IVAU` -> `DSB ISH` -> `ISB`) execute deterministically with exact cache line coverage.

---

## ADR-198-04: CPU-DMA Coherency & Exclusion Protocol

### Context
The simulated synthetic DMA controller (`VirtualDmaEngine` / `VirtualDmaDevice`) operates on physical memory addresses. DMA writes bypassing the CPU exclusive monitor would violate ARMv8 multi-copy atomicity and cause silent lost updates in concurrent lock-free data structures.

### Decision
1. Connect `VirtualDmaEngine` directly to `SmpSharedMemory` and `SmpExclusiveMonitorManager`.
2. When the DMA engine completes a write transfer to physical range $[PA, PA + Len)$, it automatically invokes `invalidate_reservations` across all CPU cores for all 64-byte granules spanned by the transfer.
3. Validate descriptor bounds before any transfer to prevent host buffer overflows. Reject cyclic descriptor chains and enforce `max_descriptors_per_chain = 256` to prevent denial-of-service hangs.

### Consequences
- Concurrent CPU threads holding `LDXR` reservations are immediately signaled when DMA overwrites their buffers, ensuring atomic store failure and re-reading DMA data.
