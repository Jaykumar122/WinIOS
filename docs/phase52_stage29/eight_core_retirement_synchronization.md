# Eight-Core Retirement Synchronization & Accounting

## 1. Dual-Level Accounting Model
To guarantee determinism in an 8-core cluster, instruction retirement is accounted for on two complementary levels:
1. **Global Retirement Sequence ($R_{global}$)**: Total instructions retired across all cores in the entire cluster since reset/restore.
2. **Per-Core Retirement Counters ($R_c$, for $c \in [0..7]$)**: Monotonically increasing instruction counters tracking progress for each core independently.

## 2. Parity Enforcement
When `config_.enforce_retirement_parity = true` is enabled in `SmpReplayCoordinator`:
- At each `QuantumEnd`, the instructions retired by the active core in that slice are compared against `r.payload1`.
- Any mismatch immediately raises `SmpReplayDivergenceType::RetirementMismatch`.
- Diagnostic information logged includes:
  - Divergent core ID
  - Expected retirement count vs actual retirement count
  - Current Program Counter ($PC$)
  - Global retirement sequence number

## 3. Verification
In Hardening Test Group 6, an artificial perturbation to `QuantumEnd.payload1` (substituting 9999 for actual retirement) was tested; `SmpReplayCoordinator` halted replay immediately and flagged `RetirementMismatch` at the exact slice boundary.
