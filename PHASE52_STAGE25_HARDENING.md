# WinIOS — Phase 52, Stage 25: Hardening & Security Audit

## 1. Architectural Safeguards
- **Replay Guardrail**: Multi-core configurations must NEVER be admitted into the single-core replay engine. `ReplaySession::prepare` actively checks `core_count > 1` and rejects execution with `ReplayErrorCode::UnsupportedConfiguration`.
- **Instruction Retirement Discipline**: Instructions faulting before retirement (synchronous exceptions) do not increment instruction counters. This eliminates divergence during replay where a faulting instruction would otherwise count as retired.
- **Strict Little-Endian Wire Protocol**: All multi-byte integer conversions are strictly enforced via explicit little-endian encoders and decoders.
- **Resource Saturation & Bounded Allocation**: Validator checks `blk.payload_bytes <= max_block_payload_bytes` (64 MiB limit) before allocating buffers, completely preventing out-of-memory denial-of-service from tampered length fields.
- **Fail-Safe Crash Marking**: Recordings missing valid footers are strictly treated as `IncompleteRecording`. Recovery tools mark salvaged logs as `Aborted`, never promoting them to `Completed`.
- **Snapshot Canonical Hash Binding**: Base snapshots must match on canonical cluster hash, preventing logs from being applied to divergent initial states.
- **Repository Guardrails**: Protected roadmap (`WinIOS_README.md`) and progress (`docs/progress.md`) remain untouched (0 edits). Pinned Inferno checkout remains clean at commit `cc4302a99167abec69b714cfd00c38caece7e7de`.
