# WINIOS CAMPAIGN 194: STAGE 194.11 REPORT
## SMP Scheduling & CPU Affinity

### 1. Executive Summary
Stage 194.11 establishes the symmetric multiprocessing (SMP) multi-core scheduler architecture for the WinIOS synthetic ARM64 kernel runtime.
Key deliverables:
- **Per-Core Run Queues & State:** Multi-core support (up to 8 cores) with private 32-level priority run queues and dedicated idle threads per virtual CPU core.
- **CPU Affinity Enforcement:** Strict enforcement of thread `cpu_affinity()` bitmasks during initial placement, voluntary yield, and load balancing.
- **Inter-Processor Interrupts (SGIs):** Integration with GICv3 Software Generated Interrupts (SGI 1: `kRescheduleSgi`) for cross-core rescheduling notifications via `ICC_SGI1R_EL1`.
- **Dynamic Load Balancing:** Work-stealing load balancer that identifies core workload imbalances and migrates eligible ready threads while strictly respecting CPU affinity masks.

---

### 2. Architecture & Design

#### 2.1 SMP Core State & Topology
`SmpScheduler` manages an array of `SmpCoreState` structures representing each CPU core in the virtual SoC:
```cpp
struct SmpCoreState {
    KernelThread* current_thread{nullptr};
    KernelThread* idle_thread{nullptr};
    std::uint32_t ready_bitmap{0};
    std::array<std::deque<KernelThread*>, kNumPriorityLevels> run_queues;
    std::uint64_t total_context_switches{0};
    std::uint64_t total_ticks{0};
};
```
- **Idle Threads:** Each core $c$ instantiates a dedicated priority-0 idle thread pinned strictly to core $c$ (`cpu_affinity = 1ULL << c`).
- **Affinity Routing (`select_best_core`):** Threads are placed on the least-loaded core matching their affinity bitmask.

#### 2.2 SGI Cross-Core Rescheduling
When a core prepares a thread affine to another core, it issues a GICv3 SGI:
```cpp
const std::uint64_t sgi1r = (static_cast<std::uint64_t>(kRescheduleSgi) << 24u) | (1ULL << target_core);
gic.send_sgi(sender_core, sgi1r);
```
Target core's redistributor receives pending SGI 1, triggering an asynchronous exception to execute `schedule()`.

#### 2.3 Load Balancing Algorithm (`balance_load`)
1. Evaluates total ready thread count across all cores to identify $C_{max}$ (highest load) and $C_{min}$ (lowest load).
2. If load imbalance $\ge 2$ threads, scans $C_{max}$'s queues from lowest priority upwards.
3. If an enqueued thread has an affinity mask allowing execution on $C_{min}$, migrates it to $C_{min}$.
4. Pinned threads whose affinity excludes $C_{min}$ are skipped, guaranteeing hard affinity invariants.

---

### 3. Verification & Results

Unit test suite `tests/unit/stage194_11_smp_scheduler_tests.cpp` executes 6 test cases:
1. `test_smp_initialization_and_per_core_idle`: Validates 4-core initialization, private idle threads, and affinity pinning.
2. `test_cpu_affinity_mask_enforcement`: Confirms thread placement strictly honors single-core and multi-core affinity bitmasks.
3. `test_multi_core_independent_dispatch_and_ticks`: Verifies parallel independent dispatching and context progression across Core 0 and Core 1.
4. `test_load_balancing_and_thread_migration`: Verifies work-stealing migration across cores for unconstrained threads.
5. `test_affinity_constrained_load_balancing`: Verifies that pinned threads are never migrated despite heavy load imbalance.
6. `test_inter_processor_interrupt_sgi_dispatch`: Validates GICv3 SGI 1 generation and target redistributor interrupt assertion.

Both Release and Debug configurations build cleanly under `/W4 /WX /permissive-` and pass all 6 tests with zero errors.

---

### 4. Classification
- Source implementations: `GUEST_SCHEDULER` / `SYNTHETIC_KERNEL`
- Simulation / host harness: `HOST_MODEL`
