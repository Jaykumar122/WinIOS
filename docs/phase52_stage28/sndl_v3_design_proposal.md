# SNDL v3 Design Proposal
## Streaming Compression, Vector Clocks & Parallel Decoding for Next-Gen SNDL

---

## 1. Motivation

As multi-core execution expands to longer runs and higher event densities, `.sndl v2` can be optimized with:
1. **Event Record Compression**:
   - Delta-encoding of timestamps and retirement sequences.
   - Run-length encoding of sequential quantum stepping events.
   - Expected compression ratio: 4x to 8x size reduction over uncompressed fixed 80-byte records.
2. **Vector Clocks**:
   - Embedding causality vector timestamps $(C_0, C_1, \dots, C_7)$ alongside the scalar global retirement sequence to support causal debugging and out-of-order trace inspection.
3. **Parallel Stream Decoding**:
   - Independent chunk synchronization markers allowing multi-threaded chunk decompression and parallel replay verification.

---

## 2. Proposed SNDL v3 Architecture

- **Format Identifier**: Magic `WSND`, Major Version `3`.
- **Chunk Container**: Fixed 128KB chunks with independent Zstandard frame compression.
- **Index Block**: Tail index mapping global retirement milestones to byte offsets for $O(\log N)$ seeking.
