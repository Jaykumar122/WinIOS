# WinIOS Campaign 224 — Stage 224.1 Forensic Architecture Audit

## 1. Executive Summary
A comprehensive audit of the Campaign 223 codebase was conducted, focusing on packaging (`src/packaging/`), storage (`src/storage/`), security (`src/security/`), IPC (`src/runtime/guest_ipc.*`), lifecycle management (`src/kernel/app_lifecycle_manager.*`), and the Category C ABI (`src/kernel/user_syscall_dispatcher.*`). While Campaign 223 successfully demonstrated multi-window rendering and interactive ARM64 application execution, significant architectural gaps exist that prevent WinIOS from functioning as a robust, managed application platform.

---

## 2. Forensic Audit Findings by Subsystem

### 2.1 WIOSPKG2 & Package Management (`src/packaging/`)
- **Package Parsing Robustness:** `AppPackage::unpack` processes 32-bit `mlen`, `dsize`, and 16-bit `plen` without full integer-overflow guards during offset arithmetic (`off + len > size`).
- **Unbounded Allocations:** `file_count` is read directly from package bytes; an adversarial package with `file_count = 0xFFFFFFFF` can force unbounded allocations or out-of-memory panics.
- **Duplicate Entry Exploitation:** `unpack` does not verify uniqueness of `FileEntry::path`. Malicious packages can include duplicate paths to overwrite earlier files.
- **Executable Validation Gap:** `PackageManifest::is_valid()` verifies that `executable_name` exists in `files`, but never inspects whether the file contains valid ARM64 Mach-O headers, segments, or entry points before staging.
- **Missing Update/Rollback Pipeline:** `PackageInstaller::install` rejects any bundle ID already present in `ApplicationRegistry` (`InstallStatus::AlreadyInstalled`). There is no atomic upgrade, backup of previous versions, or automated rollback upon post-install validation failure.
- **Unsafe Uninstall While Running:** `PackageInstaller::uninstall` removes application directories without checking if the target application is currently executing, leaving running processes with dangling VFS handles and orphaned display surfaces.

### 2.2 Security, Identity & Capabilities (`src/security/`, `src/kernel/`)
- **Caller Provenance & Forgery:** Several Category C syscalls accept guest-specified `pid` parameters. A guest thread can claim any PID to manipulate foreign windows, query foreign storage, or forge IPC sender identities.
- **Static PID 1 Privilege:** `CapabilityManager::check` contains a hardcoded bypass (`return pid == 1`), granting all permissions unconditionally without capability audits.
- **Permission Declaration vs Granting:** Applications declaring permissions in manifests are not systematically mapped to verified security tokens; capability checks are scattered rather than enforced uniformly at service boundaries.

### 2.3 Sandboxed Storage (`src/storage/`)
- **Quota Calculation Flaw:** `IsolatedStorage::current_usage_bytes()` only aggregates files in the root sandbox folder (`readdir(root_path_)`). Subdirectories created via nested paths bypass quota enforcement completely.
- **Overwrite Penalty Bug:** In `atomic_write`, quota validation computes `current_usage + size > quota_bytes_`. When updating an existing file near quota, the file's old size is not subtracted, causing valid updates to be spuriously rejected.
- **Nested Crash Recovery:** `recover_transactions()` only cleans `.tmp.` files in `root_path_`, leaving stale temporary files in subdirectories orphaned indefinitely.

### 2.4 IPC Reliability & Lifecycle (`src/runtime/guest_ipc.*`)
- **Stale Handle Reuse:** `GuestIpcRouter` generates sequential 32-bit integers (`next_port_id_++`). When ports are destroyed and re-allocated, old handles can access newly allocated ports (ABA problem). Generation-based handles are required.
- **Unauthenticated Sender:** `GuestIpcRouter::Send` takes an unverified `sender_pid`. Any process can spoof messages from system services or other apps.
- **Port Discovery Absence:** While `Port` stores a `name`, there is no lookup service (`LookupPort(const std::string& name)`), forcing processes to hardcode integer port IDs or exchange them out-of-band.

### 2.5 Process Supervision, Diagnostics & Configuration
- **Crash Context Deficit:** When guest applications fail, crash diagnostics lack structured execution records: guest PC, instruction retirement count, fault address, and failure categorization are lost.
- **Missing Configuration Service:** Applications lack an isolated, type-safe key-value configuration system with quota and namespace boundaries.
- **Unstructured Logging:** Guest logging (`kSysWiniosLog`) passes raw strings directly without level filtering, timestamps, or application origin tags.

---

## 3. Remediation Architecture Plan (Campaign 224 Roadmap)
1. **Stage 224.2 - 224.8:** Modular Platform Services, Hardened WIOSPKG2 format, Application Manifest v1, Application Registry v2, Staged Atomic Install, Safe Uninstall, and Versioned Update with Rollback.
2. **Stage 224.9 - 224.12:** Central Application Identity Token, Boundary Capability Enforcement, Recursive Quota Sandboxed Storage, and Transactional Atomic Recovery.
3. **Stage 224.13 - 224.14:** IPC Reliability v2 with 64-bit generation handles, authenticated senders, named port lookup, and process crash cleanup.
4. **Stage 224.15 - 224.18:** Process Supervisor v2, Structured Crash Diagnostics, Typed Configuration Service, and Leveled Logging Service.
5. **Stage 224.19 - 224.22:** Desktop Launcher Integration, SDK v1.8, and 2 New Native ARM64 Platform Applications (`settings.bin` & `messenger.bin`).
6. **Stage 224.23 - 224.24:** Adversarial Security Fuzzing, Regression Suite, and Release Closeout.
