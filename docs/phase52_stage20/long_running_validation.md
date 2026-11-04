# Long-Running Workload & Streaming Validation

## 1. Objectives & Stress Criteria

A deterministic replay engine can succeed on micro-tests (dozens of instructions) while failing catastrophically on realistic guest workloads (hundreds of thousands or millions of instructions). Long-running replay introduces severe stress factors:
1. **Multi-Block Buffer Streaming**: SNDL files write events in 64 KB data chunks. Replay cursors must cross chunk boundaries seamlessly without dropping events or misaligning records.
2. **Memory Footprint Stability**: Frequent checkpointing must not leak memory or cause heap fragmentation.
3. **Chunk Checksum Integrity**: Damaged, truncated, or bit-flipped blocks must be detected deterministically via CRC32C before processing.

---

## 2. Multi-Block Streaming Mechanics

The `ReplayEventCursor` and underlying `SndlReader` maintain a fixed-size 64 KB streaming window:
- When the cursor reaches the end of block $B_k$, the reader validates the 32-bit CRC32C checksum embedded in the block header.
- If the checksum matches, the reader streams block $B_{k+1}$ into the circular buffer.
- Pointer arithmetic and ring indexing are managed with zero dynamic allocations during active instruction execution.

---

## 3. Adversarial Stress & Corruption Handling (Test Group G)

Test Group G (`test_group_g_streaming_stress` in `tests/unit/replay_hardening_tests.cpp`) verified:
1. **Multi-Block Stream**: Created a multi-block `.sndl` log containing 1,500 records spanning multiple 64 KB blocks. Streamed and verified all 1,500 records sequentially without a single dropped or misordered entry.
2. **Corrupted Block Detection**: Artificially corrupted block payloads by flipping bits in the stream. The reader caught the CRC32C mismatch during block streaming, returned `SndlErrorCode::ChecksumMismatch`, and terminated replay safely with `ReplayCompletionStatus::Failed` without crashing the host process.

---

## 4. Extended Workload Benchmarking (Benchmarks 2 & 4)

In Release mode benchmarking on `replay_hardening_benchmark.exe`:
- **500,000 Recorded Events**: Streamed and dispatched across multi-block boundaries in 109.0 ms, yielding **4.59 Million events per second** (218.0 ns per event).
- **500,000 Instructions Replayed**: Executed under full state hash checkpointing every 200,000 instructions in 443.24 ms (**1.13 MIPS**).
- **Process Memory Profile**: RSS (Resident Set Size) remained completely flat throughout the 500,000-instruction run, confirming that checkpoint structures recycle fixed hash containers and do not allocate unbounded memory.
