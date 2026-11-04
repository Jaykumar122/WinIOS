# SNDL v2 Four-Core Wire Format Compatibility

## 1. Wire Format Invariance

A primary requirement of Stage 27 is that multi-core SNDL v2 wire format is preserved with **zero modification**:
- Record Size: Exactly 80 bytes per `SndlV2Record`.
- Chunk Size: Bounded chunk buffers with CRC32C chunk checksums.
- Header Magic: `SNDL2\0\0\0` (Version 2).

---

## 2. Four-Core Representation in Existing Fields

No new fields or breaking wire format changes were introduced:
- `core_id`: 32-bit integer, directly represents core indices $0, 1, 2, 3$.
- `global_event_sequence`: 64-bit continuous sequence, monotonically increasing across all four cores.
- `global_retirement_sequence`: 64-bit cluster retirement counter.
- `core_retirement_count`: 64-bit core-local retirement counter.
- `payload1` / `payload2`: 64-bit payloads accommodate all MMIO, entropy, interrupt, and timer event values.

SNDL v1 and single-core SNDL v2 logs continue to parse and replay with 100% backward compatibility.
