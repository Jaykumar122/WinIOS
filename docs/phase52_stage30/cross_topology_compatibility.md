# Cross-Topology Compatibility & Rejection Guarantees

## 1. Verified Topologies
The WinIOS architecture strictly validates topology consistency between:
1. Base snapshot (`.wsnap v2`) header core count.
2. SNDL v2 recording header core count.
3. `SmpReplayCapabilityConfig::max_supported_cores`.

### 1.1 Valid Symmetric Pairs
- 1 Core Snapshot + 1 Core SNDL + 1 Core Coordinator: PASS
- 2 Core Snapshot + 2 Core SNDL + 2 Core Coordinator: PASS
- 4 Core Snapshot + 4 Core SNDL + 4 Core Coordinator: PASS
- 8 Core Snapshot + 8 Core SNDL + 8 Core Coordinator: PASS

### 1.2 Invalid Cross-Topology Pairs (Rejection Matrix)
| Snapshot Cores | Recording Cores | Target Coordinator | Preparation Result | Error Code |
|---|---|---|---|---|
| 1 | 2 | 2 | REJECTED | `TopologyMismatch` |
| 2 | 4 | 4 | REJECTED | `TopologyMismatch` |
| 4 | 8 | 8 | REJECTED | `TopologyMismatch` |
| 8 | 4 | 4 | REJECTED | `TopologyMismatch` |
| 4 | 2 | 2 | REJECTED | `TopologyMismatch` |
| 2 | 1 | 1 | REJECTED | `TopologyMismatch` |

## 2. Rejection Invariant Guarantees
When a topology mismatch or unsupported configuration is detected during `prepare()`:
- Guest execution instructions retired: **0** (strictly zero instructions).
- Event reader cursor position: **0** (frozen, unconsumed).
- Replay coordinator state: `Prepared = false`, `Running = false`.
- Capability gate for >8 cores: Replay configuration with `max_supported_cores > 8` is unconditionally rejected with error `TopologyUnsupported`.
