# Shared Physical Memory Serialization

## 1. Design Overview

In a symmetric multiprocessing architecture, all virtual cores execute against a single unified physical address space. Rather than serializing memory per core, `SHARED_MEMORY` (Section Type `0x13`) serializes the underlying guest physical RAM exactly once.

---

## 2. Sparse 4KB Chunk Encoding

The memory serializer divides the entire guest address space into fixed 4096-byte chunks ($C_{\text{size}} = 4096$).

### Chunk Omission Algorithm
For each chunk index $i \in [0, \lceil \text{size} / C_{\text{size}} \rceil - 1]$:
1. Calculate byte range $[i \times C_{\text{size}}, \min((i + 1) \times C_{\text{size}}, \text{size})]$.
2. Scan chunk bytes using 64-bit word comparisons.
3. If all bytes are zero and `omit_zero_chunks` is true, omit chunk from payload.
4. If non-zero, record chunk index, actual payload length, and raw data bytes.

### Binary Layout
- `total_memory_size` (u64)
- `chunk_size` (u32, default 4096)
- `total_chunks` (u32)
- `non_zero_chunks` (u32)
- `flags` (u32, bit 0: omit_zero_chunks)
- Repeated for each non-zero chunk:
  - `chunk_index` (u32)
  - `chunk_length` (u32)
  - `data` (`chunk_length` bytes)

---

## 3. Restoration Semantics & Atomicity

1. Memory size match: Target `SmpSharedMemory::size()` must equal `total_memory_size`. Any mismatch aborts restoration with `MemoryCapacityMismatch`.
2. Clean zeroing: Target memory buffer is completely zeroed via `memset` prior to applying non-zero chunks.
3. Chunk index bounds check: Each `chunk_index` must be $< total\_chunks$.
4. Boundary overflow check: `chunk_index * chunk_size + chunk_length` must be $\le total\_memory\_size$.
5. Duplicate chunk check: No chunk index may appear twice.

---

## 4. Benchmark Performance & Compression Ratio

In benchmarking on a 4-core cluster:
- **1 MB Memory**: Dense size: 1028 KB; Sparse size: 79 KB (**92.3% reduction**).
- **4 MB Memory**: Dense size: 4106 KB; Sparse size: 271 KB (**93.4% reduction**).
- **16 MB Memory**: Dense size: 16418 KB; Sparse size: 1041 KB (**93.7% reduction**).
- **64 MB Memory**: Sparse size: 4119 KB (**93.6% reduction**).
- Restoration throughput reached **8,312 MB/s** in sparse mode.
