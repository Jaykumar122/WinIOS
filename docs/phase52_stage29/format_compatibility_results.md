# Multi-Topology Format Compatibility Results

## 1. Scope
Ensuring backward compatibility and architectural consistency across snapshots and SNDL streams for 1, 2, 4, and 8 virtual cores.

## 2. Compatibility Test Matrix
Hardening Test Group 14 and Unit Test Groups 16, 17, 18 evaluated format compatibility:

| Topology | Snapshot Format | SNDL Format | Replay Engine | Compatibility Result | Status |
|---|---|---|---|---|---|
| 1 Core | `.wsnap v1` / `.wsnap v2` | `.sndl v1` / `.sndl v2` | `ReplaySession` / `SmpReplayCoordinator` | Bit-identical reconstruction | **PASS** |
| 2 Cores | `.wsnap v2` | `.sndl v2` | `SmpReplayCoordinator` | Bit-identical reconstruction | **PASS** |
| 4 Cores | `.wsnap v2` | `.sndl v2` | `SmpReplayCoordinator` | Bit-identical reconstruction | **PASS** |
| 8 Cores | `.wsnap v2` | `.sndl v2` | `SmpReplayCoordinator` | Bit-identical reconstruction | **PASS** |
| >8 Cores | Any | Any | Any | Explicit rejection | **PASS** |

## 3. Structural Stability
All `.wsnap v2` files conform to 64-byte headers, run-length compressed memory, and 64-byte integrity footers. All `.sndl v2` files conform to 64-byte headers, 48-byte block headers, 80-byte records, and 64-byte footers.
