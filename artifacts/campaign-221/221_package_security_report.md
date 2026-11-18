# WinIOS Package Security & Installation Audit Report

**Campaign:** 221 (Stage 221.17)  
**Subsystem:** Package Validation, Cryptographic Verification & Sandboxing  

---

## 1. Security Verification Pipeline

Before a `.wios` package is committed to the filesystem, `PackageInstaller` executes five security validation gates:

1. **Magic Verification:** Verifies header begins with `WIOSPKG1` or `WIOSPKG2`.
2. **Manifest Schema Validation:**
   - `bundle_id` cannot contain `..`, `/`, or `\\`.
   - `executable_name` must point to a file present within the container.
   - Declared permissions are audited against supported capability identifiers.
3. **Path Traversal Defense:** Every file entry path is scanned. Absolute paths (starting with `/` or `\\`) and traversal sequences (`..`) are immediately rejected.
4. **SHA-256 Digest Verification:** Computes the cryptographic digest of every payload file and cross-references against `fe.sha256_hex`. Any mismatch aborts installation with `IntegrityCheckFailed`.
5. **Transactional Staging & Rollback:** All files are initially written to an ephemeral staging directory (`/staging/<bundle_id>/`). If any write fails, `rollback_staging()` unlinks all created files and restores the clean state.
