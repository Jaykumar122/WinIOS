# WinIOS Campaign 224 — Stage 224.6 Package Installation & Staged Deployment

## 1. Installation Lifecycle
`PackageInstaller::install` executes an atomic 6-stage deployment pipeline:

```
[Raw Package (.wios)]
         │
         ▼ (1. Validation)
   Manifest & Format Check
         │
         ▼ (2. Pre-Flight)
   Registry & Version Audit
         │
         ▼ (3. Integrity)
   File SHA-256 Digest Verification
         │
         ▼ (4. Staging)
   Extract to /staging/<bundle_id>/
         │
         ▼ (5. Commit)
   Move to /Applications/<bundle_id>.app/
         │
         ▼ (6. Registration)
   Publish to ApplicationRegistry
```

---

## 2. Failure Handling & Staging Rollback
- If extraction fails midway through writing files into `/staging/`, `rollback_staging()` is invoked to recursively wipe the staging folder.
- If committing into `/Applications/<bundle_id>.app/` encounters an I/O fault, the staging directory is cleaned and the target directory is wiped.
- At no point is an application marked registered in `ApplicationRegistry` until all files are successfully written, verified, and committed.
