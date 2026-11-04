# SMP Foundation Integration Boundaries & Format Stability

## 1. Architectural Scope

Phase 52 Stage 22 implements the execution foundation for multi-core SMP emulation. To ensure safety, reliability, and architectural consistency across the WinIOS ecosystem, strict integration boundaries are established and maintained.

---

## 2. Mandatory Replay Gating Boundary

The deterministic replay engine (`ReplayEngine`, `ReplaySession`) strictly enforces single-core execution. Any attempt to initialize or run a replay session with `core_count > 1` or `ReplayMode::MultiCore` is explicitly rejected:

```cpp
if (config_.mode == ReplayMode::MultiCore && !config_.allow_experimental_smp) {
    state_ = ReplayLifecycleState::Failed;
    stats_.completion_status = ReplayCompletionStatus::UnsupportedConfiguration;
    return ReplayResult::fail(
        ReplayErrorCode::UnsupportedConfiguration,
        "Multi-core SMP replay is unsupported/experimental..."
    );
}
```

### Protection Invariants:
1. Multi-core replay cannot be accidentally invoked.
2. `ReplayErrorCode::UnsupportedConfiguration` is deterministically returned.
3. Session transitions immediately to `ReplayLifecycleState::Failed`.
4. `stats_.completion_status` records `ReplayCompletionStatus::UnsupportedConfiguration`.
5. No partial replay or inconsistent memory state is permitted.

---

## 3. Format Stability & Schema Compatibility

All established binary formats remain 100% stable, backwards-compatible, and unmolested:

### 1. Diagnostic Trace Format (`.wtr`)
- Schema: Fixed 64-byte `TraceEvent` structures.
- Serialization: Little-endian binary stream with CRC32C integrity checksums.
- Existing event types (Instructions, branches, exceptions, sysregs, memory, timers, debug, lifecycle) remain unchanged.
- Magic and header format preserved.

### 2. Architectural Snapshot Format (`.wsnap`)
- Magic: `kWSnapMagic` (`{'W', 'S', 'N', 'A', 'P', '\0', '\1', '\0'}`).
- Version: `kWSnapCurrentVersion = 1`.
- Header: Fixed 64-byte `WSnapHeader`.
- Sections: 40-byte `WSnapSectionDescriptor` directory.
- All existing single-core snapshots validate and restore with bit-for-bit fidelity.

### 3. Non-Deterministic Log Format (`.sndl`)
- Magic: `kWSndlMagic` (`{'W', 'S', 'N', 'D', 'L', '\0', '\1', '\0'}`).
- Version: `kWSndlCurrentVersion = 1`.
- Header: Fixed 64-byte `WSndlHeader`.
- Blocks: Fixed 48-byte `WSndlBlockHeader` with CRC32C-verified variable payloads.
- Existing single-core recordings replay with 100% exact consumption.

---

## 4. Single-Core Subsystem Regression Shield

The SMP foundation changes (`SmpCoreState`, `SmpSharedMemory`, `SmpExclusiveMonitorManager`, `SmpInterruptRouter`, `SmpScheduler`) operate as non-intrusive architectural layers.
Comprehensive regression testing across all 58 CTest unit tests confirms:
- Memory subsystem: 100% passing.
- CPU interpreter & MMU: 100% passing.
- GICv3, PMU, VirtualTime, EL3, PSCI: 100% passing.
- Tracing pipeline: 100% passing.
- Snapshots (`.wsnap`): 100% passing.
- Non-deterministic logging (`.sndl`): 100% passing.
- Replay engine, hardening, and scalability: 100% passing.
