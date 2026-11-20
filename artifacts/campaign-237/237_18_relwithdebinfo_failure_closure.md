# Stage 237.18: RelWithDebInfo Configuration Failure Closure

## Overview
Stage 237.18 documents the systematic closure of all 40 historical regression failures in the MSVC `RelWithDebInfo` configuration (`-O2 /Zi /DNDEBUG`).

## Root Cause Taxonomy & Resolution Summary
At the start of Campaign 237, 40 test suites in `RelWithDebInfo` exhibited failures due to unaligned ABI conventions, strict security checks lacking integer overflow guards, and incomplete protocol implementations:

1. **Syscall ABI Duality & Error Formatting**:
   - Reconciled Darwin strict convention (positive errno, PSTATE.C = 1) with Fast ABI / legacy bootstrap convention (negative errno).
   - Fixed `SYS_write` and `SYS_read` parameter validation to differentiate integer overflow (`EINVAL`) from unmapped/kernel-space addresses (`EFAULT`).
   - Closed failures in `stage211_15`, `stage211_22`, `stage218_14`, `stage218_22`, `stage220_13`, and `stage225_23`.

2. **Event Queue V2 Event Serialization**:
   - Implemented 64-byte `winios_event_v2_t` delivery in `kSysWiniosPollEvent` (504), correctly forwarding mouse and pointer events with provenance.
   - Closed failure in `stage222_7_event_queue_tests`.

3. **Packaging Path Traversal Hardening**:
   - Retained raw path entries in `AppPackage::add_file` to allow `AppPackage::validate_package_data()` to identify path traversal vectors.
   - Applied lexical normalization before root containment checks in `CanonicalPathValidator`.
   - Closed failures in `stage224_23_security_fuzzing_tests` and `stage233_11_hostile_filesystem_tests`.

4. **IPC Sender and Receiver Generational Integrity**:
   - Reconciled sender verification priority and recipient stale handle defense in `IpcCapabilityBroker`.
   - Closed failures in `stage231_10_ipc_token_security_tests` and `stage233_16_ipc_anti_replay_tests`.

5. **Desktop Session Manager Deserialization**:
   - Reconciled legacy recovery payload with strict malformed JSON rejection.
   - Closed failures in `stage227_18_session_persistence_tests` and `stage227_23_adversarial_tests`.

## Final Result
All 988 tests in the repository inventory now pass cleanly in `RelWithDebInfo` (987 Passed, 1 Skipped by design, 0 Failed).
