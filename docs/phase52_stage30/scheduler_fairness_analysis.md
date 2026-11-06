# Scheduler Fairness & Starvation Avoidance Analysis

## 1. Scheduling Policy
The WinIOS multicore scheduler implements deterministic round-robin quantum allocation across runnable cores. The quantum is defined in guest instruction retirements ($Q$).

## 2. Equal Workload Fairness (8 Cores)
- Configured instruction quantum: $Q = 25$ instructions.
- Total scheduled instructions: 1,600 instructions (8 quanta per core).
- Empirical instruction distribution across 8 cores:
  - Core 0: 200 instructions retired (8 quanta)
  - Core 1: 200 instructions retired (8 quanta)
  - Core 2: 200 instructions retired (8 quanta)
  - Core 3: 200 instructions retired (8 quanta)
  - Core 4: 200 instructions retired (8 quanta)
  - Core 5: 200 instructions retired (8 quanta)
  - Core 6: 200 instructions retired (8 quanta)
  - Core 7: 200 instructions retired (8 quanta)
- Result: **Zero variance**. Fairness ratio is exactly 1.000. No core experienced starvation or early preemption under equal workload.

## 3. Asymmetric Workload and WFI Ineligibility
- Scenario: Core 0 active; Cores 1–7 placed in `CoreLifecycleState::WaitingForInterrupt` (WFI).
- Total scheduled instructions: 200 instructions.
- Execution outcome:
  - Core 0 retired all 200 instructions over 8 allocated quanta.
  - Cores 1–7 retired 0 instructions and consumed 0 quanta.
  - No CPU spinning or wasted scheduler ticks occurred on dormant cores.

## 4. Wake-Up from WFI
- Core 3 was transitioned from `WaitingForInterrupt` to `Runnable`.
- Core 3 immediately resumed participation in the round-robin schedule on the subsequent tick without disturbing the retirement counters of Cores 0–2 or 4–7.
