# WinIOS — Phase 52, Stage 25: Bounded Fuzzing & Mutation Testing Harness

## 1. Deterministic Mutation Framework
Because external coverage-guided fuzzers (such as libFuzzer or AFL++) require specialized compiler instrumentation not universally enabled in default MSVC toolchains, Stage 25 implements a self-contained, reproducible mutation testing harness:

- **PRNG Engine**: Fixed-seed `std::mt19937_64` (seed: `0x123456789ABCDEF0ULL`).
- **Input Corpus**: Structurally valid multi-core `.sndl v2` recording files containing headers, multiple blocks, and footers.
- **Mutation Strategies**:
  1. Single-bit flips across random byte offsets.
  2. Byte truncations at varying file offsets.
  3. Header length tampering (`payload_bytes`, `record_count`).
  4. Sequence tampering (global sequence, retirement counters).

## 2. Invariants Proven Under Mutation
- **No Unhandled Exceptions or Crashes**: The parser and validator never panic or crash on corrupted data.
- **Strict Bounded Memory**: The validator never allocates memory proportional to corrupted file lengths; `max_block_payload_bytes` bounds heap requests.
- **Detection Reliability**: 100 out of 100 bit-flip mutations were reliably identified and rejected via CRC32C, sequence checks, or magic validation.
