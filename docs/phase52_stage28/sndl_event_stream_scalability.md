# SNDL Event Stream Scalability
## SNDL v2 Stream Handling, Event Density & 8-Core Record Capacity

---

## 1. Stream Format Compatibility

The SNDL v2 wire format (`WSndlV2Header` and `WSndlV2Record`) natively accommodates 8 cores:
- **Wire Record Size**: Fixed 80 bytes per event record.
- **Core ID Field**: `core_id` is an 8-bit unsigned integer (`uint8_t`), easily supporting core values `0..7`.
- **Global Event Sequence**: `global_event_sequence` (`uint64_t`) strictly orders all events across all cores.
- **Per-Core Retirement**: `core_retirement_count` (`uint64_t`) verifies per-core execution point.

---

## 2. Event Stream Validation in Stage 28

1. **Topology Scalability Group 6**:
   - Streamed synthetic multi-core events interleaved across core IDs 0 through 7.
   - Core ID bounds check verified: IDs 0..7 pass, ID 8+ rejected with `InvalidCoreId`.
2. **Chunk Management & Buffer Scaling (Group 7)**:
   - Chunk size of 64KB holding 819 records tested under streaming conditions.
   - CRC32C per chunk ensures stream integrity.
   - Throughput: Stream writer processes > 1,000,000 events/second without backpressure.
