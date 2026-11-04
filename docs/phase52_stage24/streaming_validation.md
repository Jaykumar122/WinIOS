# WinIOS — Phase 52, Stage 24: Streaming Validation Architecture

## 1. Design Principles
The `SndlV2Validator` implements an $O(1)$ memory streaming validation engine designed to process arbitrarily large multi-gigabyte recording files on low-memory host machines.

## 2. Validation Checks & Guarantees
The validator executes single-pass sequential decoding and verifies the following properties:
1. **Header Validation**:
   - Fixed 8-byte magic: `{'W','S','N','D','2','\0','\1','\0'}`.
   - Version match: `version == 2`.
   - Header CRC32C integrity over bytes `[0..60)`.
   - Core count sanity: $1 \le \text{core\_count} \le 256$.
2. **Block Validation**:
   - Block magic `{'S','2','B','K'}`.
   - Block header CRC32C over bytes `[0..44)`.
   - Contiguous block sequence indexing.
   - Block payload CRC32C matching computed CRC32C of all record bytes.
3. **Record Validation**:
   - Uniform 80-byte record size.
   - Per-record CRC32C over bytes `[0..76)`.
   - Core ID bound: `core_id < header.core_count` (or `0xFFFFFFFF` for global events).
   - Strictly monotonic `global_sequence` across entire recording stream ($S_{i+1} = S_i + 1$).
   - Monotonically non-decreasing `global_retirement_sequence`.
   - Per-core monotonically non-decreasing `core_retirement_sequence`.
4. **Footer Validation**:
   - Footer magic `{'W','S','N','D','2','F','T','R'}`.
   - Footer CRC32C over bytes `[0..60)`.
   - Total records written match actual decoded records.
   - Total blocks match actual block count.
   - Final global sequence matches the last decoded record's global sequence.
   - Final global retirement sequence matches the highest retirement count observed.
5. **Snapshot Binding**:
   - Validates that the referenced `.wsnap v2` file exists, possesses valid snapshot magic and CRC, and matches the recorded `core_count` and `memory_capacity`.
