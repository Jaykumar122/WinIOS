# Four-Core Deterministic Workloads

## 1. Synthetic Workload Suite

To validate 4-core replay under various stress conditions, eight isolated synthetic workloads were implemented:

1. **Workload 1 — Four-Core Arithmetic**:
   - 4 independent cores computing arithmetic progressions (`MOVZ`, `ADD`, `B .`).
   - Fixed quantum budget (20 instructions/slice).
   - Validated in `test_group_4_scheduler_coordination`.

2. **Workload 2 — Shared Memory Interleaving**:
   - 4 cores writing to distinct pages of shared RAM.
   - Verified in `test_group_7_shared_memory_determinism`.

3. **Workload 3 — Exclusive Monitor Contention**:
   - 4 cores contending for a single cache-line granule.
   - Validated in `test_group_8_exclusive_monitor_contention`.

4. **Workload 4 — Multi-Core Interrupt Scheduling**:
   - SPI lines asserted across cores 0, 1, 2, 3.
   - Validated in `test_group_9_interrupt_replay`.

5. **Workload 5 — Virtual Timer Comparators**:
   - Simulated comparator expirations on virtual timers.
   - Validated in `test_group_10_timer_replay`.

6. **Workload 6 — Mixed Workload**:
   - Combines arithmetic, shared RAM writes, and external events.
   - Validated in `test_group_14_repeated_replay_10_runs`.

7. **Workload 7 — DMA Workload**:
   - Tested as explicit rejection in `test_group_12_dma_rejection`.

8. **Workload 8 — Clock Workload**:
   - Verified in `test_group_13_host_clock_virtualization`.
