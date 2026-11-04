# Failure Injection & Adversarial Robustness Results

## 1. Failure Injection Test Suite

In `test_group_16_failure_injection`, intentional corruption was applied to evaluate immediate halting semantics and divergence diagnostics:

1. **Snapshot Byte Mutation**:
   - One byte at offset 100 in `baseline_4c.wsnap` was bit-flipped.
   - Result: `SmpSnapshotManager::validate_file` detected CRC32C failure; coordinator immediately rejected preparation with `SnapshotValidationFailed`.

2. **Core ID Boundary Mutation**:
   - An event with `core_id >= 4` was fed to the coordinator.
   - Result: Coordinator immediately halted with `SmpReplayDivergenceType::CoreMismatch` and transitioned to `SmpReplayState::Diverged`.

3. **Quantum Mismatch Mutation**:
   - Truncated quantum slice was replayed against an un-truncated recording.
   - Result: Immediate halt with `SmpReplayDivergenceType::QuantumMismatch`.

4. **DMA Injection Attempt**:
   - Record with `HostInputInjected` encountered when `allow_uncaptured_dma = false`.
   - Result: Immediate halt with `SmpReplayDivergenceType::UnsupportedExternalInput`.

5. **Wall-Clock Injection Attempt**:
   - Record with `CounterReadResult` encountered when `clock_source = UnsupportedHostClock`.
   - Result: Immediate halt with `SmpReplayDivergenceType::UnsupportedExternalInput`.

In all adversarial test cases, execution stopped cleanly at the exact divergence step without partial corruption or cascading failure.
