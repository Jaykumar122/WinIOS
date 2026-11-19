# Stage 232.13 — Crash Injection and Recovery Harness Implementation & Evidence

**Campaign:** 232  
**Stage:** 232.13  
**Component:** `tests/unit/stage232_13_crash_recovery_tests.cpp`  
**Test Executable:** `build/RelWithDebInfo/stage232_13_crash_recovery_tests.exe`  
**CTest Target:** Test #932 (`stage232_13_crash_recovery_tests`)

---

## 1. Test Harness Design

The Stage 232.13 test harness provides programmatic fault injection into the `PackageInstaller` state machine. It simulates unexpected host crashes, power loss, or process termination at every discrete phase of application installation, update, and recovery.

### Fault Injection Mechanisms
1. `PackageInstaller::inject_fault_at_state(InstallerState state)`: Simulates an immediate, unhandled crash at a given state transition. The transaction writes the state to its journal file and aborts execution immediately without executing normal destructor or rollback cleanup routines.
2. `PackageInstaller::set_fault_injection_hook(FaultInjectionHook hook)`: Allows external observation or mutation of state at every transition boundary.
3. Post-Crash Recovery Simulation: The harness instantiates a new `PackageInstaller` instance with the unmodified VFS and registry, invoking `reconcile_and_recover()`.

---

## 2. Test Execution & Coverage

### Suite Summary
- `test_crash_injection_all_precommit_states()`:
  - Iterates through all 8 pre-commit states: `Initiated`, `Validating`, `ManifestParsed`, `SecurityVerified`, `Staged`, `Extracting`, `Extracted`, `Registering`.
  - Asserts that when a crash occurs at any pre-commit state, `reconcile_and_recover()` cleanly unwinds the transaction.
  - Verifies that zero files are left in `/Applications/<bundle_id>.app`, zero orphaned staging directories remain in `/staging/`, and the application is not marked as installed in `ApplicationRegistry`.
- `test_crash_at_committed_state()`:
  - Injects a fault immediately after writing the `Committed` journal state (simulating crash before staging cleanup).
  - Verifies that `reconcile_and_recover()` detects the committed state with valid target contents, registers the application, and cleans up staging.
- `test_update_crash_restores_backup()`:
  - Installs v1 ("1.0.0").
  - Injects fault during update to v2 ("2.0.0") during `Extracting`.
  - Verifies that `reconcile_and_recover()` restores the pristine v1 backup directory, preserves the v1 registry entry, and wipes staging.
- `test_journal_tampering_defense()`:
  - Creates a staging journal with an invalidated SHA-256 integrity checksum.
  - Verifies that recovery detects the cryptographic violation, discards the transaction, and purges the invalid staging directory without applying any unverified state.

---

## 3. Verification Log Output

```text
========================================================
STAGE 232.13: Crash Injection and Recovery Harness Tests
========================================================
[Stage 232.13] Testing crash injection across all pre-commit states...
[Stage 232.13] Testing crash at Committed state and recovery...
[Stage 232.13] Testing update crash recovery restores pristine backup...
[Stage 232.13] Testing journal tampering defense and recovery...
All Stage 232.13 tests passed successfully!
```

CTest Result:
```text
    Start 932: stage232_13_crash_recovery_tests
8/8 Test #932: stage232_13_crash_recovery_tests ............   Passed    0.02 sec
```
Status: **PASSED (100%)**
