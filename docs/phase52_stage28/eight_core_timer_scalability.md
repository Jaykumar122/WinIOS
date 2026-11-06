# Eight-Core Timer Scalability
## Deterministic Hardware Timer Virtualization Across Cores 0..7

---

## 1. Virtual Time Subsystem

`VirtualTime` provides guest-visible virtualized time across all cores, strictly isolating guest execution from host wall-clock non-determinism:
- **Physical Counter**: Monotonic counter `CNTPCT_EL0` derived strictly from retired instructions or deterministic ticks.
- **Virtual Counter**: `CNTVCT_EL0 = CNTPCT_EL0 - CNTVOFF_EL2`.
- **Per-Core Comparators**: Each core maintains independent timer comparators (`CNTP_CVAL_EL0`, `CNTV_CVAL_EL0`).

---

## 2. Eight-Core Timer Stress Workload (Workload 5)

- **Scenario**: 8 virtual cores program distinct timer deadlines:
  $$\text{Deadline}[i] = T_{\text{base}} + (i + 1) \times 1000 \text{ ticks}$$
- **Execution**:
  - Virtual time advances monotonically.
  - When $T \ge \text{Deadline}[i]$, PPI 30 is fired deterministically on core $i$.
  - All 8 timers fire in exact chronological sequence.
  - No timer fires prematurely or out-of-order.
- **Result**: PASSED. Deterministic timer progression verified across 8 cores.
