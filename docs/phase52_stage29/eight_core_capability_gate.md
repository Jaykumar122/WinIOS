# Eight-Core Replay Capability Gate Architecture

## 1. Overview
The capability gate enforces deterministic constraints and prevents accidental activation of experimental multi-core features. For 8-core replay, an explicit dual opt-in policy is enforced across `SmpReplayCoordinator`, `ReplaySession`, and the CLI.

## 2. Configuration Parameters
In `winios::replay::SmpReplayCapabilityConfig` and `winios::replay::ReplayConfig`:
```cpp
bool allow_experimental_smp{false};  // Required for any multi-core replay
bool allow_four_core_replay{false};  // Stage 27 opt-in for 4 cores
bool allow_eight_core_replay{false}; // Stage 29 opt-in for 8 cores
```

## 3. Capability Gate Truth Table
The following authorization matrix was exhaustively verified in Hardening Test Group 13:

| Topology | `allow_experimental_smp` | `allow_four_core_replay` | `allow_eight_core_replay` | Outcome | Replay Error Code |
|---|---|---|---|---|---|
| 8 Cores | `false` | `false` | `false` | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `true`  | `false` | `false` | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `false` | `true`  | `false` | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `true`  | `true`  | `false` | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `false` | `false` | `true`  | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `false` | `true`  | `true`  | **REJECTED** | `UnsupportedConfiguration` |
| 8 Cores | `true`  | `false` | `true`  | **AUTHORIZED** | `Success` |
| 8 Cores | `true`  | `true`  | `true`  | **AUTHORIZED** | `Success` |
| >8 Cores | `true` | `true`  | `true`  | **REJECTED** | `UnsupportedConfiguration` |

## 4. CLI Integration
The `win_ios` CLI provides the flag:
```
--allow-eight-core
```
When provided alongside `--smp-mode`, both `allow_experimental_smp` and `allow_eight_core_replay` are enabled for the session.
