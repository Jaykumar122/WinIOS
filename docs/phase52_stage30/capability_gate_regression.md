# Replay Capability Gate & Security Regression Analysis

## 1. Safety Gates Enforcement
Deterministic replay on multicore topologies involves strict capability gates to protect against unintended state corruption:

1. **Default Gating**:
   - `SmpReplayCapabilityConfig::allow_experimental_smp = false` (default)
   - `SmpReplayCapabilityConfig::allow_eight_core_replay = false` (default)
   - `SmpReplayCapabilityConfig::allow_uncaptured_dma = false` (unconditionally false)

2. **Eight-Core Dual Opt-in Requirement**:
   To replay an 8-core SNDL v2 recording, both flags must be explicitly set:
   ```cpp
   cfg.allow_experimental_smp = true;
   cfg.allow_eight_core_replay = true;
   ```
   If either flag is omitted or false, preparation fails immediately with `SmpReplayStatus::CapabilityViolation`.

3. **DMA Access Gating**:
   Asynchronous or uncaptured host DMA operations are strictly rejected (`allow_uncaptured_dma = false`). Attempts to configure uncaptured DMA fail assertion checks at startup.

## 2. Regression Matrix Across Supported Configurations
- Single-Core Replay: Unconditional support (no SMP flags required). PASS.
- Two-Core Replay: Supported when `allow_experimental_smp = true`. PASS.
- Four-Core Replay: Supported when `allow_experimental_smp = true && allow_four_core_replay = true`. PASS.
- Eight-Core Replay: Supported only when `allow_experimental_smp = true && allow_eight_core_replay = true`. PASS.
- 16-Core Replay: Explicitly rejected across all configurations. PASS.
