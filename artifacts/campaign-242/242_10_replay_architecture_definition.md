# Stage 242.10: Replay Architecture and Versioned State Definition

**Campaign**: 242
**Repository**: D:\WinISO26\WinIOS
**Branch**: campaign-191r

## Summary
Defined the formal versioned replay-state contract in `src/runtime/replay_state.h`:

- **Header Specification**:
  - Magic: `0x57494F535245504CULL` ("WIOSREPL").
  - Version: `kReplayCurrentVersion = 1`.
  - Payload Limit: 16 MB maximum buffer to prevent memory exhaustion attacks.
- **Contract Domains**:
  1. Architectural CPU State: GPRs X0-X30, PC, SP, NZCV, instructions executed, exit code.
  2. Memory State: Root table PA, ASID, overall memory FNV-1a hash, sparse page hashes.
  3. Syscall Stream: Sequence of recorded syscall numbers, arguments, and return codes.
