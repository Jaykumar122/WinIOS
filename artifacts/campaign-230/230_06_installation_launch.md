# WinIOS Campaign 230 — Stages 230.6–230.9 Installation & Launch Architecture

## 1. Installation Transaction Hardening (Stage 230.6)
`PackageInstaller` operates under strict transactional guarantees:
- **Isolated Staging**: Incoming package files are extracted to `/staging/<bundle_id>` first.
- **Quota Validation**: Cumulative uncompressed payload size is validated against `manifest.storage_quota_bytes`.
- **Pre-Publication Integrity**: Every file is SHA-256 hashed and verified before promotion.
- **Atomic Promotion**: The target directory `/Applications/<bundle_id>.app` is populated atomically.
- **Failure Rollback & Interrupted Recovery**: Any write error automatically triggers recursive cleanup (`rollback_staging`). In addition, `recover_interrupted_installations()` scans and wipes orphaned staging trees upon startup.
- **Working Installation Preservation**: During an update, the working installation is backed up to `/staging/<bundle_id>.backup`; if the update fails, the working version is immediately restored.

## 2. Application Registry & Version Management (Stage 230.7)
`ApplicationRegistry` tracks:
- `bundle_id`, `name`, `display_name`, `version`, `package_version` (`3.0`), `min_sdk_version`, `target_abi`.
- `state`: Explicit lifecycle states (`Installed`, `Upgrading`, `Corrupted`, `Uninstalled`).
- `verify_installed_app_integrity()`: Cross-checks registry records with VFS contents to detect corrupted or missing files, transitioning faulty packages to `InstallationState::Corrupted`.
- `supported_doc_types` and default document associations (`set_default_app_for_type`).

## 3. Compatibility Resolution (Stage 230.8)
Pre-launch validation verifies:
- Minimum SDK requirements: Rejects apps requiring `min_sdk_version > kCurrentPlatformSdkVersion` (`0x00020400`).
- Target ABI compatibility: Validates Category C ABI range (`2.3` – `2.4`).
- Architecture: Enforces `arm64` freestanding EL0 Mach-O binary requirements.
- Capability constraints: Blocks apps declaring restricted or prohibited privileges (e.g. `SystemAdmin`, `KernelAccess`).

## 4. Unified Application Launch Pipeline (Stage 230.9)
`AppLaunchPipeline` coordinates 12 sequential launch stages:
1. Application resolution in `ApplicationRegistry`.
2. Installation integrity verification via `verify_installed_app_integrity`.
3. Binary inspection via `AppInspector::inspect_binary` verifying 64-bit Mach-O headers, ARM64 architecture, entry point commands (`LC_MAIN`/`LC_UNIXTHREAD`), and clean W^X page permissions.
4. SDK & ABI version compatibility checks.
5. Capability authorization checks against `CapabilityManager`.
6. Monotonic PID & generation allocation in `ProcessIdentityManager`.
7. Task registration in `TaskManager` with generation validation.
8. Resource budget allocation in `ResourceBudgetManager`.
9. Capability binding and permission grants in `CapabilityManager`.
10. Event and log publication in `DiagnosticsService`.
11. Safe rollback on any intermediate stage failure (zero PID or task leakage).
12. Controlled lifecycle termination releasing capabilities, task records, and memory budgets, and transitioning PID to the quarantine ring.
