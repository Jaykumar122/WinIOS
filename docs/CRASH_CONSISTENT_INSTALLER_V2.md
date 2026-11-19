# WinIOS Crash-Consistent Installer v2 Architecture
## Campaign 232 — Developer & Systems Guide

This document describes the 11-state Write-Ahead Log (WAL) installation engine implemented in `src/packaging/package_installer.h/.cpp` for WinIOS Campaign 232.

---

## 1. Architectural Principles

The WinIOS v2 installer provides:
- **Atomicity**: Package installation either fully succeeds or completely rolls back to the prior state.
- **Consistency**: Application manifest, binaries, and registry state are validated before committing.
- **Durability & Crash Recovery**: A persistent Write-Ahead Log (`install.journal`) ensures that crashes at any stage can be cleanly recovered or rolled back upon platform boot.
- **Tamper Resistance**: Every journal record includes a monotonic sequence number and a SHA-256 integrity checksum.

---

## 2. The 11-State Transaction Machine

The installer transitions through 11 well-defined lifecycle states:

```text
[Idle] 
  │
  ▼
[Staging] ───(Fault)───► [RollingBack] ──► [RolledBack] ──► [Idle]
  │
  ▼
[SignatureVerifying] ──(Fault)──► [RollingBack]
  │
  ▼
[ManifestVerifying] ──(Fault)──► [RollingBack]
  │
  ▼
[BackupCreated] ──(Fault)──► [RollingBack]
  │
  ▼
[Committing] ──(Fault)──► [RollingBack]
  │
  ▼
[Committed] (Atomic cut-over point)
  │
  ▼
[CleaningUp]
  │
  ▼
[Idle]
```

---

## 3. Crash Recovery Workflow

Upon startup or invocation of `PackageInstaller::reconcile_and_recover()`:
1. The installer inspects `install.journal` in the application root directory.
2. If the journal is empty or absent, state is clean (`Idle`).
3. If the journal contains active records:
   - Validates the SHA-256 checksum of each record. If corrupted, halts with `JournalTampered`.
   - If the last verified state is **prior** to `Committed`:
     - Restores any pre-existing application version from `.backup`.
     - Deletes orphaned `.staging` directories.
     - Logs recovery event and clears journal.
   - If the last state is `Committed` or `CleaningUp`:
     - Finalizes installation by deleting obsolete `.backup` directories.
     - Transitions state to `Idle`.

---

## 4. Upgrade Safety

During an application upgrade:
1. Existing files in `/apps/<bundle_id>` are atomically moved to `/apps/<bundle_id>.backup`.
2. Staged files in `/apps/<bundle_id>.staging` are atomically renamed to `/apps/<bundle_id>`.
3. If failure occurs during extraction or validation, `/apps/<bundle_id>.backup` is atomically restored.
4. Old user data in `/data/<bundle_id>` is preserved and not overwritten.
