# WinIOS — Fault Injection & Hardening Results
## Adversarial Testing Matrix for Crash & Corruption Resilience

---

## 1. Fault Injection Test Matrix

The test suite `dma_crash_recovery_tests` exercises 8 targeted fault injection groups:

| Group | Injected Fault Description | Recovery Action | Verified Outcome | Status |
|---|---|---|---|---|
| **1** | Interrupted SNDL stream (process killed during write) | Scanner detects missing completion trailer, scans to last valid block | Salvages all complete events; marks stream `Recovered` | **PASS** |
| **2** | Truncated SNDL file mid-block | Scanner detects partial block, drops partial trailing bytes | Salvages up to last complete block; stream replays cleanly | **PASS** |
| **3** | Bit-flipped block data (storage corruption) | Scanner validates block CRC32C, detects corruption | Halts sweep before corrupt block; isolates valid prior blocks | **PASS** |
| **4** | Missing companion `.dma` payload file | Replay coordinator checks file presence | Rejects playback with `FileNotFound`; refuses corrupt execution | **PASS** |
| **5** | Mismatched session UUID between SNDL and WDMA | Replay coordinator verifies session binding | Rejects playback with `SessionMismatch`; prevents substitution | **PASS** |
| **6** | Corrupted DMA payload bytes (in-memory/NVMe bitrot) | Replay coordinator calculates payload CRC32C | Rejects replay at exact corrupt event with `CorruptedPayload` | **PASS** |
| **7** | Stream truncated midway through DMA transaction | Scanner trims backward to drop orphan `DmaTransferBegin` | Stream ends at clean transaction boundary; replays without error | **PASS** |
| **8** | Corrupted WDMA tail index table | Scanner falls back to linear payload parsing | Reconstructs payload index table dynamically; salvages stream | **PASS** |

---

## 2. Invariant Verification

Across all 8 failure injection groups:
1. **No Silent Data Corruption:** In no case did the system replay incorrect, corrupted, or synthetic data.
2. **Explicit Error Signaling:** All rejections returned deterministic, documented error codes.
3. **Clean Stream Parity:** Any salvaged stream replayed identically to an uncorrupted recording stopped at the same boundary.
