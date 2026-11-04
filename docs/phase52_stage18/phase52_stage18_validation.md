# WinIOS Phase 52 Stage 18: SNDL Validation and CLI Inspection Subsystem

## 1. Overview

The SNDL validation and inspection subsystem (`src/sndl/sndl_validation.h` and `sndl_validation.cpp`) provides non-mutating, zero-allocation audit tools to inspect, verify, and summarize `.sndl` files before they are accepted for replay.

---

## 2. Validation Architecture

Validation operates across multiple integrity layers:

```
+---------------------------------------------------------------+
| Layer 1: Structural File Sanity                              |
| * Minimum file size (>= 176 bytes = Header + 1 Blk + Footer)  |
| * Little-endian format version check (version == 1)           |
+---------------------------------------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Layer 2: Checksum & Header Integrity                          |
| * Header Magic ("WSNDL\0\1\0") & Header CRC32C                |
| * Footer Magic ("WSNDLFTR") & Footer CRC32C                   |
| * Finalization Status (Completed vs Overflow / Aborted)       |
+---------------------------------------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Layer 3: Block & Record Stream Integrity                      |
| * Block Magic ("SBLK") & Block Header CRC32C                  |
| * Block Payload CRC32C matching record data                   |
| * Record count consistency across blocks                      |
+---------------------------------------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Layer 4: Global Monotonic Sequence & Causal Integrity         |
| * Global sequence continuity (seq_k+1 == seq_k + 1)           |
| * Per-core retirement monotonicity                            |
| * Sub-instruction ordinal continuity                          |
+---------------------------------------------------------------+
                                |
                                v
+---------------------------------------------------------------+
| Layer 5: Snapshot Association Binding                         |
| * Base snapshot file existence check                          |
| * Bit-for-bit baseline snapshot header CRC32C matching        |
| * Core count compatibility                                    |
+---------------------------------------------------------------+
```

---

## 3. CLI Command Suite

The WinIOS command-line interface exposes three dedicated subcommands under `win_ios sndl`:

### 3.1 `win_ios sndl inspect <file.sndl>`
Provides detailed inspection of file headers, block counts, event taxonomy statistics, and sample records:
```
$ win_ios.exe sndl inspect sample.sndl
=== WinIOS Sparse Non-Deterministic Log (SNDL) Inspection ===
File Size:            304 bytes
Format Version:       1
Core Count:           2
Base Snapshot Hash:   0xdeadbeefcafe0001
Header CRC32C:        0x85313fed
Total Blocks:         1
Cleanly Finalized:    YES
Final Record Count:   2
Final Sequence Num:   2
Total Instructions:   150
Final Status:         Completed (Clean)

--- Nondeterministic Event Taxonomy ---
  [ 1] MMIO_READ_RESULT             : 1
  [2 ] EXTERNAL_INTERRUPT_INJECT    : 1
  [3 ] INTERRUPT_DELIVERY           : 0
  [4 ] TIMER_EXPIRATION             : 0
  [5 ] COUNTER_READ_RESULT          : 0
  [6 ] ENTROPY_RESULT               : 0
  [7 ] HOST_INPUT                   : 0
  [8 ] EXTERNAL_DEVICE_INPUT        : 0
  [9 ] OTHER_EXTERNAL_INPUT         : 0

--- First 2 Sample Records ---
  [Seq 1   ] Core 0 | RetCnt 10     (Sub 0) | PC 0x80001000 | MMIO_READ_RESULT (DuringInstruction) | P1: 0x1c000000 P2: 0x42
  [Seq 2   ] Core 1 | RetCnt 0      (Sub 0) | PC 0x0 | EXTERNAL_INTERRUPT_INJECT (AsynchronousInput) | P1: 0x30 P2: 0x20
```

### 3.2 `win_ios sndl summary <file.sndl>`
Displays execution statistics, event counts, and high-level causal overview (alias for inspect).

### 3.3 `win_ios sndl validate <file.sndl> [--snapshot <file.wsnap>]`
Runs full end-to-end cryptographic and structural validation. If `--snapshot` is supplied, validates that the `.sndl` recording is bound to that baseline snapshot:
```
$ win_ios.exe sndl validate sample.sndl --snapshot baseline.wsnap
SNDL validation SUCCESS: sample.sndl
  SNDL file is structurally valid and complete
  Records: 2 | Blocks: 1
  Baseline Snapshot Match: YES
```

If validation fails, exits with non-zero code and provides diagnostic reasons:
```
$ win_ios.exe sndl validate corrupted.sndl
SNDL validation FAILED: Block payload CRC32C checksum mismatch at block 0
```
