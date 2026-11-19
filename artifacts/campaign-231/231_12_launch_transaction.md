# Stage 231.12 — Transactional Application Launch and Rollback

## Overview
Stage 231.12 implements a formal transactional state machine in `winios::loader::AppLaunchPipeline` (and `AppLauncher`), addressing finding SEC-231-06. If any step of the launch sequence fails (e.g., environment staging, virtual memory mapping, thread allocation, or sandbox initialization), all partially allocated resources—including process identity slots, task manager registrations, capability bindings, and memory budgets—are rolled back atomically with zero resource leaks.

## Key Implementation Details
1. **Explicit Transactional State Phases**:
   - `LaunchTransactionPhase`:
     - Phase 1: `ResolvingIdentity`
     - Phase 2: `VerifyingIntegrity`
     - Phase 3: `InspectingBinary`
     - Phase 4: `CheckingCompatibility`
     - Phase 5: `AuthorizingCapabilities`
     - Phase 6: `AllocatingProcess`
     - Phase 7: `RegisteringTask`
     - Phase 8: `AllocatingBudget`
     - Phase 9: `StagingEnvironment`
     - Phase 10: `MappingGuestMemory`
     - Phase 11: `AllocatingExecutionThread`
     - Phase 12: `InitializingSandbox`
     - Phase 13: `Committed` (Atomic commit to active running state)
     - Rollback state: `RolledBack`
2. **Atomic Rollback Mechanism**:
   - Tracks exact acquisition of:
     - Process handle slot (`identity_mgr_->allocate_process`)
     - Task registration (`task_mgr_->register_task`)
     - Resource budget allocation (`budget_mgr_->allocate_memory`)
     - Capability bindings (`cap_mgr_->bind_identity`)
     - Staged environment and virtual memory mappings
   - In the event of an error at any phase, the rollback lambda unwinds acquisitions in strict reverse order, ensuring the PID slot is terminated, task unregistered, and budget freed.
3. **Deterministic Failure Injection Hooks**:
   - `inject_failure(LaunchFailureInjectionPoint point, LaunchStatus status)` allows simulating failures at all discrete stages:
     - `FailEnvironmentStaging` -> returns `LaunchStatus::EnvironmentStagingFailed`
     - `FailMemoryMapping` -> returns `LaunchStatus::MemoryMappingFailed`
     - `FailThreadAllocation` -> returns `LaunchStatus::ThreadAllocationFailed`
     - `FailSandboxCreation` -> returns `LaunchStatus::SandboxCreationFailed`
     - Early phases (Integrity, Inspection, Compatibility, Capability, Allocation, Registration, Budget).
4. **Verification & Invariant Checking**:
   - Validated that upon rollback, active process count in `ProcessIdentityManager` returns to pre-launch baseline.
   - Validated that `TaskManager` retains no dangling task records.
   - Verified clean subsequent launch and termination following repeated failure injections.

## Verification
- **Test Suite**: `tests/unit/stage231_12_launch_transaction_tests.cpp` (CTest #914).
- **Test Results**:
  - RelWithDebInfo: Test #914 Passed in 0.07s.
  - Debug: Test #914 Passed in 0.10s.
  - Regressions: Baseline tests #900 (`stage230_18`) and #903 (`stage230_21`) passed 100%.
