# WinIOS — Phase 52, Stage 25: Compatibility & Historical Invariants

## 1. Version Compatibility Invariants
The hardening and validation features added in Stage 25 preserve 100% backward compatibility:
- **`.sndl v1`**: Single-core recording logs continue to be parsed and validated by existing v1 components without regression.
- **`.wsnap v1`**: Single-core architectural snapshots remain valid and restorable.
- **`.wsnap v2`**: Multi-core cluster snapshots remain fully compatible and restorable.
- **`.wtr v1`**: Diagnostic trace files remain unaffected.

## 2. Replay Engine Isolation & Safety Guardrails
Stage 25 is strictly an audit, hardening, and validation milestone. Multi-core replay remains **unimplemented and prohibited**.

The replay guardrail in `ReplaySession::prepare`:
```cpp
if (snapshot.header.core_count > 1 || sndl_header.core_count > 1) {
    return ReplayResult::fail(ReplayErrorCode::UnsupportedConfiguration);
}
```
Any attempt to supply an `.sndl v2` or `.wsnap v2` multi-core recording to the replay engine is deterministically rejected with `ReplayErrorCode::UnsupportedConfiguration`. Verified in Group J tests.
