# Stage 239.5: Guest Runtime Initialization & Reversible Rollback

## 1. Runtime Startup Ordering
The application launch sequence executes as an atomic 14-phase transaction managed by `AppLaunchPipeline` in `src/loader/app_launcher.h`:

1. **ResolvingIdentity**: Look up application record in `ApplicationRegistry`.
2. **VerifyingIntegrity**: Validate on-disk VFS executable and manifest hashes.
3. **InspectingBinary**: Parse Mach-O headers, verify CPU type (`ARM64`), file type (`MH_EXECUTE`), and section alignment.
4. **CheckingCompatibility**: Check `app.min_sdk_version <= kCurrentPlatformSdkVersion`.
5. **AuthorizingCapabilities**: Validate that requested permissions do not include forbidden system rights (`KernelAccess`, `SystemAdmin`).
6. **AllocatingProcess**: Request new `ProcessHandle` from `ProcessIdentityManager`.
7. **RegisteringTask**: Register running task entry in `TaskManager`.
8. **AllocatingBudget**: Allocate memory quota in `ResourceBudgetManager`.
9. **StagingEnvironment**: Construct guest environment vector and arguments block.
10. **MappingGuestMemory**: Map Mach-O `__TEXT` (RX), `__DATA` (RW), stack (RW) under W^X permissions.
11. **AllocatingExecutionThread**: Allocate host worker execution thread.
12. **InitializingSandbox**: Configure sandbox directory boundaries and isolated storage root.
13. **Committed**: Application successfully committed to running session.
14. **RolledBack**: Terminal state if any phase 1–12 fails.

## 2. Reversible Rollback Architecture
Every acquired resource is tracked by a rollback journal. If a failure or injection point occurs at phase $N$:
- Resources acquired in phases $1 \dots N-1$ are unwound in reverse order.
- Process handle is terminated and slot is quarantined; generation increments.
- Memory budgets, tasks, and capabilities are completely freed.
- Zero orphaned resources, leaked handles, or zombie records remain on host or VFS.

## 3. Findings
Runtime initialization ordering and failure reversal are verified and deterministic.
