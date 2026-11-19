# Stage 231.1 — Campaign 230 Security and Architecture Audit

## 1. Audit Scope & Methodology
A comprehensive architectural and implementation security audit was performed across Campaign 230 components in `D:\WinISO26\WinIOS\`:
- Packaging and container validation: `app_package.h`, `app_package.cpp`, `package_installer.h`, `package_installer.cpp`, `app_registry.h`, `app_registry.cpp`.
- Process and task runtime: `process_identity.h`, `app_launcher.h`, `multi_app_session_controller.h`.
- Security and IPC subsystems: `ipc_capability_broker.h`, `document_association_manager.h`, `capability_manager.h`.
- Category C ABI and dispatcher: `user_syscall_dispatcher.cpp`, `user_syscall_dispatcher.h`, `winios_sdk.h`, `app.h`.
- Developer tooling: `winios_app_cli.h`, `app_generator.h`, `debugger_workflow.h`, `app_performance_profiler.h`.

---

## 2. Severity-Ranked Findings

### 2.1 Critical & High Severity Findings

#### [SEC-231-01] [CRITICAL] Pseudo-Signature & Digest Misrepresentation as Digital Signature
- **Component**: `src/packaging/app_package.cpp` (`AppPackage::evaluate_trust`)
- **Impact**: In `evaluate_trust()`, any package with `signature_sha256.size() == 64` or `"PLATFORM_ROOT_A16_TRUSTED"` is granted `PackageTrustLevel::PlatformSigned`. A SHA-256 hash or arbitrary string was treated as a digital signature, allowing unauthenticated packages to claim platform privileges without cryptographic verification.
- **Remediation Required**: Introduce a real asymmetric signature verification scheme (e.g. Ed25519/ECDSA or HMAC-SHA256 authenticated envelope) with a publisher trust store, authenticating manifest, file paths, digests, and metadata.

#### [SEC-231-02] [HIGH] Predictable Sequential Capability Tokens in IPC Broker
- **Component**: `src/kernel/ipc_capability_broker.h` (`IpcCapabilityBroker::grant_communication_right`)
- **Impact**: Capability tokens were generated via monotonic counter `++token_counter_` (1, 2, 3...). An unprivileged rogue process can easily guess valid capability tokens of other processes and forge IPC communications.
- **Remediation Required**: Integrate cryptographically secure pseudo-random number generator (CSPRNG) via Windows system cryptography (`BCryptGenRandom`) to generate 64-bit or 128-bit unpredictable tokens bound to sender/receiver handles and generations.

#### [SEC-231-03] [HIGH] Generation Counter Silent Wrap-Around in Fixed PID Allocation
- **Component**: `src/kernel/process_identity.h` (`ProcessIdentityManager::allocate_fixed_pid`)
- **Impact**: In `allocate_fixed_pid`, if `gen` rolls over to 0, it silently sets `gen = 1`, resurrecting the original generation handle. Stale handles from generation 1 would suddenly become valid again, compromising process isolation.
- **Remediation Required**: Explicitly retire slots whose generation counters reach exhaustion, preventing any reuse of previously valid generation values.

#### [SEC-231-04] [HIGH] Non-Crash-Consistent Package Installation & Destructive Overwrite
- **Component**: `src/packaging/package_installer.cpp` (`PackageInstaller::install`, `update`)
- **Impact**: The installer removes `/Applications/<bundle_id>.app` before committing files from memory. If a failure occurs during commit (e.g., storage quota exceeded or host crash), the previous valid installation is lost and orphaned staging directories remain unrecovered.
- **Remediation Required**: Implement durable transaction metadata, isolated unique staging directories (`/staging/<tx_id>/`), atomic promotion where VFS supports it, and an idempotent startup recovery journal.

---

### 2.2 Medium Severity Findings

#### [SEC-231-05] [MEDIUM] Document Capability Grants Vulnerable to Recycled PIDs
- **Component**: `src/documents/document_association_manager.h`, `src/documents/document_registry.cpp`
- **Impact**: Document access grants store `grantee_pid` without checking or storing the process generation. If `grantee_pid` exits and the PID is recycled by another application, the new process inherits access to the unrevoked document grant.
- **Remediation Required**: Bind document grants to `ProcessHandle` (PID + generation) and verify generation on every access check.

#### [SEC-231-06] [MEDIUM] Incomplete Lifecycle Rollback on Launch Pipeline Failure
- **Component**: `src/loader/app_launcher.h` (`AppLaunchPipeline::launch`)
- **Impact**: The 12-step launch pipeline performs sequential resource acquisitions. While task creation and budget allocation have rollback logic, failure in capability binding or session entry leaves incomplete state in `TaskManager` or `ProcessIdentityManager`.
- **Remediation Required**: Implement an explicit transactional launch lifecycle state machine with structured rollback for all acquired resources.

#### [SEC-231-07] [MEDIUM] Syscall 593 (`SYS_WINIOS_APP_LIST`) Input & Output Buffer Edge Cases
- **Component**: `src/kernel/user_syscall_dispatcher.cpp` (`case kSysWiniosAppList`)
- **Impact**:
  1. `req.total_count` is not populated, violating SDK v2.4 contract.
  2. If `req.max_entries > 0` but `req.buffer_va == 0`, it does not return `EFAULT`.
  3. `req.max_entries` is not capped, leading to possible unbounded memory allocation in `out_list`.
- **Remediation Required**: Validate `buffer_va` whenever `to_return > 0`, cap maximum entries to a safe threshold (e.g. 512), and populate `req.total_count`.

#### [SEC-231-08] [MEDIUM] Path Canonicalization & Boundary Gaps in Packaging
- **Component**: `src/packaging/app_package.cpp` (`validate_package_data`)
- **Impact**: Path checks only reject `..`, leading `/` or `\\`, and `:`. They do not normalize mixed separators, reject embedded null characters, handle trailing dots/spaces, or prevent case-insensitive collisions on Windows host filesystems.
- **Remediation Required**: Introduce a unified canonical package path validator enforcing strict POSIX-style relative paths under `/Applications/<bundle_id>.app/`.

---

### 2.3 Low Severity Findings

#### [SEC-231-09] [LOW] AppGenerator Placeholder Build Scripts
- **Component**: `src/tools/app_generator.h`
- **Impact**: Generated build scripts emit placeholder paths (`<SDK_INCLUDE_PATH>`) requiring manual editing.
- **Remediation Required**: Provide resolved or relative project SDK paths.

#### [SEC-231-10] [LOW] CLI Exit Code Precision
- **Component**: `src/tools/winios_app_cli.h`
- **Impact**: Commands return binary 0 or 1 rather than distinct POSIX error codes.
- **Remediation Required**: Map platform and packaging errors to meaningful status codes.

---

## 3. Audit Summary & Action Plan
All 10 findings will be addressed systematically across Stages 231.2 through 231.20:
- Stages 231.2–231.4: Resolve SEC-231-01 and SEC-231-02 via unified crypto/randomness primitives and publisher trust store.
- Stages 231.5–231.6: Resolve SEC-231-08 via package parser hardening and canonical path enforcement.
- Stages 231.7–231.8: Resolve SEC-231-04 via crash-consistent transactions and startup recovery.
- Stages 231.9–231.11: Resolve SEC-231-03, SEC-231-02, and SEC-231-05 via process identity hardening, token security, and document grant binding.
- Stages 231.12–231.14: Resolve SEC-231-06 and SEC-231-07 via transactional launch and ABI boundary tests.
- Stages 231.15–231.20: Resolve SEC-231-09 and SEC-231-10 via toolchain verification, CLI hardening, and multi-app scheduling.
