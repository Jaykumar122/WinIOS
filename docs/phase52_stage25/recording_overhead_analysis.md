# WinIOS — Phase 52, Stage 25: Recording Overhead & Profiling Analysis

## 1. Overhead Breakdown & Investigation
Stage 24 reported 36–41% execution overhead when recording multi-core execution. Profiling revealed the primary cost drivers:
1. **Vector Push & Re-Encoding**: `SndlV2Writer` originally copied `SndlV2Record` structs into `std::vector<SndlV2Record> current_block_`, then looped through the vector on block flushes to encode into bytes.
2. **Intermediate Buffer Resizing**: Dynamic buffer allocations occurred on block boundaries.
3. **CRC32C Calculation**: 76 bytes per record + payload CRC for each block.

## 2. Hardening & Optimization in Stage 25
- **Direct Wire Serialization**: In `SndlV2Writer::write_record`, records are serialized directly into a preallocated `block_payload_buffer_` at offset `block_records_count_ * 80`.
- **Elimination of Intermediate Vector**: Vector reallocation, copying, and the second serialization pass in `flush_current_block()` were completely eliminated.
- **Batched I/O**: Contiguous 512-record blocks written via direct OS file streaming.

## 3. Before vs After Performance Comparison

| Metric | Stage 24 Reported Baseline | Stage 25 Hardened | Delta / Improvement |
|---|---|---|---|
| **Encode Rate** | 15.53 M records/sec | **15.67 M records/sec** | +0.9% |
| **Decode Rate** | 16.24 M records/sec | **15.93 M records/sec** | Equivalent |
| **Disk Write Speed** | 399.60 MB/sec | **442.42 MB/sec** | **+10.7% faster** |
| **Streaming Validation**| 2.29 M events/sec (175 MB/s) | **2.81 M events/sec (215 MB/s)** | **+22.7% faster** |
| **2-Core Sched Overhead**| 36.3% | **31.3%** | **5.0% reduction** |
| **4-Core Sched Overhead**| 36.4% | **26.6%** | **9.8% reduction** |
| **8-Core Sched Overhead**| 40.7% | **34.3%** | **6.4% reduction** |
