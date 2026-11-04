# WinIOS — Phase 52, Stage 25: Adversarial Binary Validation & Tampering Resistance

## 1. Attack Vectors Tested & Mitigated
The adversarial verification suite (`tests/unit/sndl_v2_hardening_tests.cpp`, Group E) exercises hostile inputs and corrupted wire artifacts:

1. **Header Attacks**:
   - Corrupted Magic: Byte 0 flipped to `'X'` -> `SndlV2ErrorCode::InvalidMagic`.
   - Unsupported Version: Changed to version 3 with valid recalculated CRC -> `SndlV2ErrorCode::UnsupportedVersion`.
   - Header CRC Tampering: Payload byte modified without updating CRC -> `SndlV2ErrorCode::HeaderCrcMismatch`.
2. **Block Attacks**:
   - Corrupted Block Magic: Byte 0 of block header altered -> `SndlV2ErrorCode::InvalidMagic`.
   - Block Header CRC Mismatch: Block metadata altered -> `SndlV2ErrorCode::BlockHeaderCrcMismatch`.
   - Block Payload CRC Mismatch: Record byte inside payload altered -> `SndlV2ErrorCode::BlockPayloadCrcMismatch`.
   - Block Sequence Discontinuity: Non-consecutive block IDs -> `SndlV2ErrorCode::InvalidSequence`.
3. **Footer & Resource Attacks**:
   - Truncated Footer: File cut off before complete 64-byte footer -> `SndlV2ErrorCode::TruncatedBlock`.
   - Trailing Garbage: Extraneous bytes appended after footer -> `SndlV2ErrorCode::TrailingGarbage`.
   - Excessive Block Allocation: Declared block payload size exceeding `max_block_payload_bytes` (64 MiB limit) -> `SndlV2ErrorCode::ResourceLimitExceeded`.
