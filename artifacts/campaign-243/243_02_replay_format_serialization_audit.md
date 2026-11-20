# Stage 243.2: Replay V2 Format and Serialization Audit

**Campaign**: 243
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Implemented and validated Replay V2 format in `src/runtime/replay_state.h`:

- **Magic and Version**: Magic `0x57494F535245504CULL`, Version `kReplayVersion2 = 2`.
- **Coverage Flags**: Added 32-bit `coverage_flags` bitmask for granular control (CPU GPR, control regs, memory hash, page hashes, syscall log).
- **Backward Compatibility**: Full deserializer backward compatibility; successfully parses legacy Version 1 artifacts.
- **Bounded Allocations**: Enforced upper bound on page count (`100,000`) and syscall entries (`65,536`) to prevent memory exhaustion attacks.
- **Test Target**: `stage243_2_replay_v2_serialization_tests` (PASSED).
