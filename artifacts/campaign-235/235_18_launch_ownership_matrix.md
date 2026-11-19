# Stage 235.18: Launch Ownership Matrix & Transactional Boundary Verification

## Executive Summary

Stage 235.18 verifies the end-to-end 14-phase application launch transaction (`winios::loader::AppLaunchPipeline`), process handle generation isolation (`winios::kernel::ProcessIdentityManager`), and resource ownership boundaries (`CapabilityManager`, `TaskManager`, `ResourceBudgetManager`).

## 14-Phase Launch Transaction Verification

The pipeline sequences 14 deterministic phases:
1. `ResolvingIdentity`: Looks up bundle in `ApplicationRegistry`.
2. `VerifyingIntegrity`: Validates file hashes and executable presence in VFS.
3. `InspectingBinary`: Checks ARM64 Mach-O headers, magic, and segment layout.
4. `CheckingCompatibility`: Verifies minimum SDK requirement vs platform version.
5. `AuthorizingCapabilities`: Rejects forbidden capabilities (`SystemAdmin`, `KernelAccess`).
6. `AllocatingProcess`: Obtains unique PID and generational handle from `ProcessIdentityManager`.
7. `RegisteringTask`: Registers task in `TaskManager` with task metadata.
8. `AllocatingBudget`: Reserves CPU and memory quota in `ResourceBudgetManager`.
9. `StagingEnvironment`: Configures guest runtime environment variables and paths.
10. `MappingGuestMemory`: Maps Mach-O segments adhering to 16 KB page granules and W^X boundaries.
11. `AllocatingExecutionThread`: Allocates host EL0 guest worker execution thread context.
12. `InitializingSandbox`: Enforces VFS container isolation and path confinement.
13. `Committed`: Emits audit log events to `DiagnosticsService` and returns live process handle.
14. `RolledBack`: Terminal failure state that triggers comprehensive inverse unwinding.

## Generational Process Isolation

- **Stale Handle Invalidation**: When an application instance terminates, its generational handle `{pid, generation}` is immediately deactivated.
- **Recycled Slot Protection**: When the same PID slot is reused for subsequent launches, the slot generation counter increments strictly monotonically. Any retained handle referring to an older generation is rejected by `is_valid_handle`, `terminate`, and IPC dispatchers, eliminating use-after-free and PID recycling race conditions.
- **Quarantine FIFO**: Process identity incorporates a quarantine queue delaying immediate slot reuse.

## Test Matrix & Execution

- **Test Suite**: `stage235_18_launch_ownership_tests`
- **Configuration & Timing**:
  - `RelWithDebInfo`: Passed (0.04s)
  - `Debug`: Passed (0.05s)
- **Verified Invariants**:
  1. Successful 14-phase commit with capability token generation and binding.
  2. Generational isolation on recycled PID slots preventing stale handle execution.
  3. Strict rollback from late-stage failures (e.g. sandbox creation failure) leaving zero leaked tasks, processes, or capability grants.
