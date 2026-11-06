# WinIOS — Recording Recovery Specification
## Salvage Algorithm, Transaction Validation & Isolated Recovery

---

## 1. Scanner Operation Overview

The `RecordingRecoveryScanner` is invoked whenever a recording session was interrupted or damaged. It operates with zero mutation on the source files, writing salvaged artifacts strictly to an isolated output directory.

```
+-----------------------------------------------------------+
|               RecordingRecoveryScanner                    |
|                                                           |
| 1. Phase 1: Header & Identity Validation                  |
|    - Verify SNDL magic & block schema                     |
|    - Verify WDMA magic ('VDMADATA')                       |
|    - Confirm Session ID and Snapshot Hash parity          |
|                                                           |
| 2. Phase 2: Sequential Block Sweep                        |
|    - Parse blocks sequentially, verifying Block CRC32C    |
|    - Parse individual records within valid blocks         |
|    - Track pending DMA transactions (Begin -> End)        |
|    - Track open scheduling quantums (Start -> End)        |
|                                                           |
| 3. Phase 3: Failure Point Detection                       |
|    - Halt forward sweep upon encountering:                |
|      * Block CRC mismatch                                 |
|      * Truncated record or EOF midway through block       |
|      * Corrupted DMA payload in companion store           |
|                                                           |
| 4. Phase 4: Backward Boundary Trimming                    |
|    - If trim_incomplete_trailing_dma == true:             |
|      * Roll back to last confirmed DmaTransferEnd         |
|    - If stream ends inside open quantum:                  |
|      * Roll back to last confirmed QuantumEnd             |
|                                                           |
| 5. Phase 5: Isolated Stream Generation                    |
|    - Write salvaged records into output_dir/<name>.sndl   |
|    - Extract and write salvaged DMA companion store       |
|    - Update header event counts & status (Recovered)      |
|    - Emit comprehensive RecoveryReport                    |
+-----------------------------------------------------------+
```

---

## 2. Recovery Guarantees

1. **Non-Destructive:** The source files remain untouched.
2. **Replayable Output:** Any output generated with `report.success == true` is 100% replayable by `SmpReplayCoordinator` from the base snapshot without encountering unexpected EOF or missing payload errors.
3. **Conservative Discard:** Any unclosed operations at the tail are cleanly pruned. The discarded byte count and event count are accurately tracked in `RecoveryReport`.
