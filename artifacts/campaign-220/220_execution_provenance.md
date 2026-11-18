# WinIOS Campaign 220 — Stage 220.2 Execution Provenance Architecture

## 1. Overview & Objectives

In Campaign 218, `ExecutionBoundaryEnforcer` established that only genuine guest CPU instruction retirement could increment instruction counters. However, in Campaign 219's multi-process environment, execution events needed stronger attribution:
- Explicit distinction between guest retirement, kernel syscall services, scheduler operations, host simulation, presentation rendering, and test instrumentation.
- Attribution of execution events to specific Process IDs (PID), Thread IDs (TID), and Guest Program Counters (PC).
- Monotonically increasing sequence numbers across all execution events to guarantee auditing order.
- Hardened rejection against adversarial callers attempting to increment guest instruction counts from host-only contexts.

---

## 2. Provenance Classification Schema

| ExecutionOrigin Enum | Identifier | Description | Allows Guest Instruction Retirement? |
| :--- | :---: | :--- | :---: |
| `kUnknown` | 0 | Uninitialized / unclassified origin | **NO (Rejected)** |
| `kHostSimulation` | 1 | Host-side synthetic simulation step | **NO (Rejected)** |
| `kGuestCpuRetired` | 2 | Genuine virtual CPU instruction retirement | **YES (Permitted)** |
| `kKernelSyscallService` | 3 | Host servicing of guest kernel syscall | **NO (Rejected)** |
| `kHostPresentation` | 4 | Framebuffer rendering / host presentation | **NO (Rejected)** |
| `kHostSchedulerOperation` | 5 | Multi-process scheduler context switch / queue ops | **NO (Rejected)** |
| `kTestInstrumentation` | 6 | Unit test harness / verification probe | **NO (Rejected)** |

---

## 3. Structural Implementation

In `src/runtime/execution_boundary.h`:
- `ProvenanceEvent` structure records:
  ```cpp
  struct ProvenanceEvent {
      uint64_t sequence_number;
      ExecutionOrigin origin;
      int32_t pid;
      uint64_t tid;
      uint64_t instruction_count;
      uint64_t guest_pc;
  };
  ```
- Circular event ring buffer (`kMaxRecentEvents = 256`) protected by mutex, preserving chronological execution history.
- `RecordGuestInstructionRetirementExplicit(count, pid, tid, pc)`:
  - Validates `current_origin_ == ExecutionOrigin::kGuestCpuRetired`.
  - Increments `verified_guest_instructions_retired_` atomically.
  - Generates monotonic event sequence number.
  - Rejects all other origins, incrementing `rejected_increments_` atomically and recording a rejection event.
- Monitored counters:
  - `total_scheduler_ops_`: Count of scheduler operations.
  - `total_syscall_services_`: Count of syscall services.
  - `rejected_increments_`: Count of thwarted instruction fabrication attempts.

---

## 4. Verification Evidence

Verified in `tests/unit/stage220_2_execution_provenance_tests.cpp`:
1. `test_initial_state`: Confirms zeroed initial state and reset semantics.
2. `test_genuine_guest_retirement`: Proves that genuine guest CPU execution records accurate instructions with PID, TID, and PC, generating sequenced events.
3. `test_adversarial_rejection`: Proves that attempts from all 6 non-guest origins (`kUnknown`, `kHostSimulation`, `kKernelSyscallService`, `kHostPresentation`, `kHostSchedulerOperation`, `kTestInstrumentation`) are rejected with 100% detection rate.
4. `test_scheduler_and_syscall_accounting`: Confirms that scheduler operations and syscall services are tracked separately without inflating guest instruction counters.
