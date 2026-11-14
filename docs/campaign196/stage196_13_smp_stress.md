# Stage 196.13 — SMP Scheduler Race and Migration Stress (Acceptance Test H)

## 1. Architectural Implementation
- **8-Core SMP Scheduling Invariants (`kMaxSmpCores = 8`):**
  - Per-core run queues (`kNumPriorityLevels`), ready bitmap, and dedicated per-core idle threads.
  - Multi-core tick preemption: when quantum expires or higher-priority thread arrives, preemption triggers context switch.
  - Inter-Processor Interrupt (IPI): Core-to-core reschedule notifications via `kRescheduleSgi = 1` through `GICv3::send_sgi`.
- **Thread Affinity & Dynamic Load Balancing:**
  - Bitmask CPU affinity enforcement (`cpu_affinity()`) in `select_best_core`.
  - `balance_load()` scans overloaded cores and migrates threads to underloaded cores strictly respecting bitmask affinity.
  - Invariant: A thread is never scheduled concurrently on two cores under any interleaving or migration race condition.

## 2. Test Evidence (Acceptance Test H)
Verified via `stage196_13_smp_stress_tests`:
- Test H1: 8-core scheduler initialization, 16 concurrent threads with 0xFF affinity, 200 tick cycles. Invariant verified: no duplicate thread execution across cores.
- Test H2: Affinity partitioning (cores 0-3 vs 4-7) and dynamic load rebalancing under queue imbalance.
- Test H3: Multi-core preemption triggered via GICv3 SGI 1 reschedule interrupt.
- Test H4: Heavy stress contention with 32 threads, 8 cores, 500 randomized steps (ticks, yields, blocks, unblocks, migrations). Zero duplicate thread execution verified across all iterations.

## 3. Evidence Classification
`GUEST_SMP`, `SYNTHETIC_KERNEL`. Tested in Debug and Release.
